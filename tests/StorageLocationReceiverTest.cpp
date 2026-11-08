// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/compiler/StorageLocation.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

using namespace nomad;

namespace {

class EvaluationCopyType final : public ClassType {
public:
    EvaluationCopyType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        EvaluationCopyType*& descriptor
    ):
        ClassType(runtime, id, std::move(name), mode)
    {
        descriptor = this;
    }

    void copyValue(const RuntimeValue* source, RuntimeValue* destination) const override {
        ClassType::copyValue(source, destination);
        ++copyCount;
        if (failCopies) {
            throw NomadException("Unexpected receiver copy");
        }
    }

    mutable NomadIndex copyCount = 0;
    bool failCopies = false;
};

} // namespace

BOOST_AUTO_TEST_SUITE(storage_location_receivers)

BOOST_AUTO_TEST_CASE(named_and_nested_struct_receivers_borrow_storage_without_copying_unread_fields) {
    Runtime runtime;
    EvaluationCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<EvaluationCopyType>(
        "EvaluationCopy", ClassMode::Closed, field);
    BOOST_REQUIRE(fieldId.isValid());
    const auto leafId = runtime.registerStruct("EvaluationLeaf");
    const auto* leaf = runtime.getType(leafId).value();
    const auto numberId = runtime.registerProperty(leafId, "number", runtime.getIntegerType());
    BOOST_REQUIRE(numberId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(leafId, "reference", field).isValid());
    const auto rootId = runtime.registerStruct("EvaluationRoot");
    const auto* root = runtime.getType(rootId).value();
    BOOST_REQUIRE(runtime.registerProperty(rootId, "prefix", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(rootId, "leaf", leaf).isValid());
    InterpreterContext* evaluation = nullptr;
    const auto incrementId = runtime.addMethod(leafId, {
        .name = "increment", .parameters = {}, .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [&](VirtualMachine* active) {
            auto& number = active->getStorageParameter(0, 0);
            BOOST_TEST(&number == evaluation->getVariableStorage("root").data() + 1);
            number.setIntegerValue(number.getIntegerValue() + 12);
            active->setIntegerResult(number.getIntegerValue());
        }
    });
    BOOST_REQUIRE(incrementId.isValid());
    runtime.finalizeCompositeFieldLayout(rootId);
    auto compiler = runtime.createCompiler();
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    {
        InterpreterContext context(interpreter);
        evaluation = &context;
        std::array<RuntimeValue, 3> source{
            RuntimeValue{NomadInteger{3}}, RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadString{"retained"}}
        };
        context.setVariable("root", root, source);
        root->freeValue(source.data());
        context.setResult(runtime.getIntegerType(), RuntimeValue{NomadInteger{99}});
        ScriptFunction function(CallableId{}, "evaluation", "", "");
        BOOST_REQUIRE(function.registerVariable("root", root).isValid());
        CompilerContext resolution(compiler.get(), CompilerMode::Interpreter, &context);
        NativeMemberExpression expression(nullptr, 1, 1, "root.leaf", "leaf",
            std::make_unique<IdentifierExpression>(nullptr, 1, 1, "root"), ArgumentList{}, false);
        BOOST_REQUIRE(expression.resolve(&resolution, &function));
        const InterpreterValue snapshot(root, context.getVariableStorage("root"));
        field->copyCount = 0;
        field->failCopies = true;
        const auto location = expression.getStorageLocation();
        BOOST_REQUIRE(location.has_value());
        const auto result = location->withReceiver(context, [&](InterpreterReceiver& receiver) {
            BOOST_TEST(receiver.callMethod(incrementId, {}).getValue().getIntegerValue() == 29);
            return receiver.getProperty(numberId);
        });
        BOOST_TEST(context.getVariableStorage("root")[1].getIntegerValue() == 29);
        BOOST_TEST(result.getValue().getIntegerValue() == 29);
        BOOST_TEST(snapshot.getStorage()[1].getIntegerValue() == 17);
        NativeMemberExpression read(nullptr, 1, 1, "root.leaf.number", "number",
            std::make_unique<NativeMemberExpression>(nullptr, 1, 1, "root.leaf", "leaf",
                std::make_unique<IdentifierExpression>(nullptr, 1, 1, "root"), ArgumentList{}, false),
            ArgumentList{}, false);
        BOOST_REQUIRE(read.resolve(&resolution, &function));
        BOOST_TEST(read.evaluate(context).getValue().getIntegerValue() == 29);
        BOOST_TEST(field->copyCount == 0U);
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 99);
        field->failCopies = false;
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(temporary_struct_field_operations_release_backing_storage_and_return_independent_snapshots) {
    Runtime runtime;
    const auto leafId = runtime.registerStruct("EvaluationTemporaryLeaf");
    const auto* leaf = runtime.getType(leafId).value();
    const auto numberId = runtime.registerProperty(leafId, "number", runtime.getIntegerType());
    BOOST_REQUIRE(numberId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(leafId, "text", runtime.getStringType()).isValid());
    const auto rootId = runtime.registerStruct("EvaluationTemporaryRoot");
    const auto* root = runtime.getType(rootId).value();
    BOOST_REQUIRE(runtime.registerProperty(rootId, "leaf", leaf).isValid());
    runtime.finalizeCompositeFieldLayout(rootId);
    auto calls = 0;
    const auto makeId = runtime.registerNativeFunction("makeEvaluationTemporary", [&](VirtualMachine* active) {
        ++calls;
        auto* storage = &active->getStorageParameter(0, 0);
        storage[0].setIntegerValue(17);
        storage[1].setStringValue("temporary receiver");
    }, {}, root, "");
    BOOST_REQUIRE(makeId.isValid());
    auto compiler = runtime.createCompiler();
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    CompilerContext resolution(compiler.get(), CompilerMode::Interpreter, &context);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    std::optional<InterpreterValue> snapshot;
    {
        {
            NativeMemberExpression expression(nullptr, 1, 1, "temporary.leaf", "leaf",
                std::make_unique<FunctionCallExpression>(nullptr, 1, 1, "makeEvaluationTemporary"),
                ArgumentList{}, false);
            BOOST_REQUIRE(expression.resolve(&resolution, nullptr));
            const auto location = expression.getStorageLocation();
            BOOST_REQUIRE(location.has_value());
            const auto result = location->withReceiver(context, [&](InterpreterReceiver& receiver) {
                BOOST_TEST(calls == 1);
                BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore + 1);
                snapshot = receiver.getValue();
                receiver.setProperty(numberId, InterpreterValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{29}}));
                return receiver.getProperty(numberId);
            });
            BOOST_TEST(result.getValue().getIntegerValue() == 29);
        }
        BOOST_TEST(calls == 1);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore + 1);
        BOOST_TEST(snapshot->getStorage()[0].getIntegerValue() == 17);
        BOOST_TEST(snapshot->getStorage()[1].getStringValue() == "temporary receiver");
        BOOST_TEST(snapshot->getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
    }
    snapshot.reset();
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(object_field_scopes_retain_the_owner_after_its_variable_is_cleared) {
    Runtime runtime;
    const auto leafId = runtime.registerStruct("EvaluationObjectLeaf");
    const auto* leaf = runtime.getType(leafId).value();
    const auto numberId = runtime.registerProperty(leafId, "number", runtime.getIntegerType());
    BOOST_REQUIRE(numberId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(leafId, "text", runtime.getStringType()).isValid());
    const auto ownerId = runtime.registerClass("EvaluationObjectOwner", ClassMode::Closed);
    const auto* owner = runtime.getType(ownerId).value()->asClassType();
    const auto leafProperty = runtime.registerProperty(ownerId, "leaf", leaf);
    BOOST_REQUIRE(leafProperty.isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    auto compiler = runtime.createCompiler();
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    {
        InterpreterContext context(interpreter);
        RuntimeValue object;
        object.setObjectValue(new ScriptObject(owner));
        {
            std::array<RuntimeValue, 2> source{
                RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadString{"object field"}}
            };
            const InterpreterValue value(leaf, source);
            leaf->freeValue(source.data());
            const auto receiver = interpreter.createObjectReceiver(*owner, object.getObjectValue());
            receiver->setProperty(leafProperty, value);
        }
        context.setVariable("holder", owner, object);
        object.freeObjectValue();
        const auto objectsWithOwner = NomadObject::getLiveObjectCount();
        ScriptFunction function(CallableId{}, "evaluation", "", "");
        BOOST_REQUIRE(function.registerVariable("holder", owner).isValid());
        CompilerContext resolution(compiler.get(), CompilerMode::Interpreter, &context);
        NativeMemberExpression expression(nullptr, 1, 1, "holder.leaf", "leaf",
            std::make_unique<IdentifierExpression>(nullptr, 1, 1, "holder"), ArgumentList{}, false);
        BOOST_REQUIRE(expression.resolve(&resolution, &function));
        std::optional<InterpreterValue> snapshot;
        const auto location = expression.getStorageLocation();
        BOOST_REQUIRE(location.has_value());
        const auto result = location->withReceiver(context, [&](InterpreterReceiver& receiver) {
            context.clearVariableValue("holder");
            BOOST_TEST(context.getVariableType("holder") == nullptr);
            BOOST_TEST(NomadObject::getLiveObjectCount() == objectsWithOwner);
            snapshot = receiver.getValue();
            receiver.setProperty(numberId, InterpreterValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{29}}));
            return receiver.getProperty(numberId);
        });
        BOOST_TEST(result.getValue().getIntegerValue() == 29);
        BOOST_TEST(snapshot->getStorage()[0].getIntegerValue() == 17);
        BOOST_TEST(snapshot->getStorage()[1].getStringValue() == "object field");
        BOOST_TEST(snapshot->getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(zero_sized_nested_receivers_borrow_no_slots_and_keep_their_types) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("EvaluationEmpty");
    const auto* empty = runtime.getType(emptyId).value();
    const auto rootId = runtime.registerStruct("EvaluationEmptyRoot");
    const auto* root = runtime.getType(rootId).value();
    BOOST_REQUIRE(runtime.registerProperty(rootId, "empty", empty).isValid());
    runtime.finalizeCompositeFieldLayout(rootId);
    auto compiler = runtime.createCompiler();
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    context.setVariable("root", root, std::span<const RuntimeValue>{});
    ScriptFunction function(CallableId{}, "evaluation", "", "");
    const auto variableId = function.registerVariable("root", root);
    BOOST_REQUIRE(variableId.isValid());
    BOOST_TEST(context.getMutableVariableStorageById(variableId).data() == nullptr);
    CompilerContext resolution(compiler.get(), CompilerMode::Interpreter, &context);
    NativeMemberExpression expression(nullptr, 1, 1, "root.empty", "empty",
        std::make_unique<IdentifierExpression>(nullptr, 1, 1, "root"), ArgumentList{}, false);
    BOOST_REQUIRE(expression.resolve(&resolution, &function));
    const auto location = expression.getStorageLocation();
    BOOST_REQUIRE(location.has_value());
    const auto value = location->withReceiver(context, [&](InterpreterReceiver& receiver) {
        BOOST_TEST(receiver.getType() == empty);
        return receiver.getValue();
    });
    BOOST_TEST(value.getStorage().empty());
    BOOST_CHECK_THROW(static_cast<void>(context.getMutableParameterStorage(0)), NomadBug);
    BOOST_CHECK_THROW(static_cast<void>(context.getMutableReceiverStorage()), NomadBug);
}

BOOST_AUTO_TEST_CASE(native_property_failures_release_temporary_object_receivers_and_partial_results) {
    Runtime runtime;
    const auto ownerId = runtime.registerClass("EvaluationFailureOwner", ClassMode::Closed);
    const auto* owner = runtime.getType(ownerId).value()->asClassType();
    const auto propertyId = runtime.addProperty(ownerId, {
        .valueType = runtime.getStringType(), .name = "fail",
        .getter = [](VirtualMachine*, RuntimeValue& result) {
            result.setStringValue("partial result");
            throw NomadException("Scoped property failure");
        }
    });
    BOOST_REQUIRE(propertyId.isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    const auto makeId = runtime.registerNativeFunction("makeEvaluationFailure", [owner](VirtualMachine* active) {
        active->getResult().setObjectValue(new ScriptObject(owner));
    }, {}, owner, "");
    BOOST_REQUIRE(makeId.isValid());
    auto compiler = runtime.createCompiler();
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    CompilerContext resolution(compiler.get(), CompilerMode::Interpreter, &context);
    NativeMemberExpression expression(nullptr, 1, 1, "temporary.fail", "fail",
        std::make_unique<FunctionCallExpression>(nullptr, 1, 1, "makeEvaluationFailure"),
        ArgumentList{}, false);
    BOOST_REQUIRE(expression.resolve(&resolution, nullptr));
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    BOOST_CHECK_THROW(static_cast<void>(expression.evaluate(context)), NomadException);
    BOOST_TEST(!context.hasError());
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_SUITE_END()
