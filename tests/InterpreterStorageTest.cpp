// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/interpreter/InterpreterStorage.hpp>

#include <nomad/interpreter/Interpreter.hpp>
#include <nomad/interpreter/InterpreterValue.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

using namespace nomad;

static_assert(!std::is_copy_constructible_v<InterpreterStorage>);
static_assert(!std::is_copy_assignable_v<InterpreterStorage>);
static_assert(std::is_same_v<decltype(std::declval<const InterpreterStorage&>().getStorage()),
    std::span<const RuntimeValue>>);
static_assert(std::is_same_v<decltype(std::declval<InterpreterStorage&>().getMutableStorage()),
    std::span<RuntimeValue>>);

namespace {

class FailingStorageCopyType final : public ClassType {
public:
    FailingStorageCopyType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        FailingStorageCopyType*& descriptor
    ):
        ClassType(runtime, id, std::move(name), mode)
    {
        descriptor = this;
    }

    void copyValue(const RuntimeValue* source, RuntimeValue* destination) const override {
        ClassType::copyValue(source, destination);
        if (++copyCount == failCopyAt) {
            throw NomadException("Interpreter storage copy failed");
        }
    }

    mutable NomadIndex copyCount = 0;
    NomadIndex failCopyAt = 0;
};

} // namespace

BOOST_AUTO_TEST_SUITE(interpreter_storage)

BOOST_AUTO_TEST_CASE(scalar_storage_retains_references_and_preserves_addresses_on_assignment) {
    Runtime runtime;
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    RuntimeValue source{NomadString{"original"}};
    RuntimeValue replacement{NomadString{"replacement"}};
    {
        InterpreterStorage storage(runtime.getStringType(), {&source, 1});
        const auto* address = storage.getStorage().data();
        BOOST_TEST(storage.getType() == runtime.getStringType());
        BOOST_TEST(storage.getMutableStorage().data() == address);
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == 2U);
        const InterpreterValue snapshot(storage.getType(), storage.getStorage());
        storage.assign({&replacement, 1});
        BOOST_TEST(storage.getStorage().data() == address);
        BOOST_TEST(storage.getStorage()[0].getStringValue() == "replacement");
        BOOST_TEST(snapshot.getValue().getStringValue() == "original");
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == 2U);
        BOOST_TEST(replacement.getObjectValue()->getReferenceCount() == 2U);
        storage.assign(storage.getStorage());
        BOOST_TEST(storage.getStorage().data() == address);
        BOOST_TEST(replacement.getObjectValue()->getReferenceCount() == 2U);
    }
    BOOST_TEST(source.getObjectValue()->getReferenceCount() == 1U);
    BOOST_TEST(replacement.getObjectValue()->getReferenceCount() == 1U);
    source.freeObjectValue();
    replacement.freeObjectValue();
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(object_storage_retains_shared_identity_without_copying_the_object) {
    Runtime runtime;
    const auto ownerId = runtime.registerClass("StorageObject", ClassMode::Closed);
    const auto* owner = runtime.getType(ownerId).value()->asClassType();
    runtime.finalizeCompositeFieldLayout(ownerId);
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    RuntimeValue source;
    source.setObjectValue(new ScriptObject(owner));
    {
        InterpreterStorage storage(owner, {&source, 1});
        BOOST_TEST(storage.getStorage()[0].getObjectValue() == source.getObjectValue());
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == 2U);
        storage.assign(storage.getStorage());
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == 2U);
    }
    BOOST_TEST(source.getObjectValue()->getReferenceCount() == 1U);
    source.freeObjectValue();
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(struct_storage_supports_live_receiver_mutation_and_independent_snapshots) {
    Runtime runtime;
    const auto ownerId = runtime.registerStruct("MutableStorageStruct");
    const auto* owner = runtime.getType(ownerId).value()->asStructType();
    const auto numberProperty = runtime.registerProperty(ownerId, "number", runtime.getIntegerType());
    BOOST_REQUIRE(numberProperty.isValid());
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    {
        std::array<RuntimeValue, 2> source{
            RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadString{"owned"}}
        };
        InterpreterStorage storage(owner, source);
        owner->freeValue(source.data());
        const auto* address = storage.getStorage().data();
        const auto receiver = interpreter.createStructReceiver(*owner, storage.getMutableStorage().data());
        const InterpreterValue snapshot(owner, storage.getStorage());
        const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{29}});
        receiver->setProperty(numberProperty, number);
        BOOST_TEST(storage.getStorage()[0].getIntegerValue() == 29);
        BOOST_TEST(snapshot.getStorage()[0].getIntegerValue() == 17);
        BOOST_TEST(snapshot.getStorage().data() != address);
        storage.getMutableStorage()[0].setIntegerValue(41);
        BOOST_TEST(receiver->getProperty(numberProperty).getValue().getIntegerValue() == 41);
        storage.assign(storage.getStorage());
        BOOST_TEST(storage.getStorage().data() == address);
        BOOST_TEST(storage.getStorage()[1].getStringValue() == "owned");
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(zero_sized_storage_preserves_type_and_accepts_empty_assignment) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("MutableStorageEmpty");
    const auto* empty = runtime.getType(emptyId).value()->asStructType();
    runtime.finalizeCompositeFieldLayout(emptyId);
    InterpreterStorage storage(empty, {});
    BOOST_TEST(storage.getType() == empty);
    BOOST_TEST(storage.getStorage().empty());
    BOOST_TEST(storage.getStorage().data() == nullptr);
    BOOST_TEST(storage.getMutableStorage().data() == nullptr);
    storage.assign({});
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto receiver = interpreter.createStructReceiver(*empty, storage.getMutableStorage().data());
    BOOST_TEST(receiver->getValue().getType() == empty);
    BOOST_TEST(receiver->getValue().getStorage().empty());
    RuntimeValue number{NomadInteger{7}};
    BOOST_CHECK_THROW(storage.assign({&number, 1}), std::invalid_argument);
    BOOST_TEST(storage.getType() == empty);
}

BOOST_AUTO_TEST_CASE(invalid_storage_widths_are_rejected_without_changing_contents) {
    Runtime runtime;
    RuntimeValue number{NomadInteger{7}};
    BOOST_CHECK_THROW((InterpreterStorage{nullptr, {}}), std::invalid_argument);
    BOOST_CHECK_THROW((InterpreterStorage{runtime.getIntegerType(), {}}), std::invalid_argument);
    InterpreterStorage storage(runtime.getIntegerType(), {&number, 1});
    const auto* address = storage.getStorage().data();
    BOOST_CHECK_THROW(storage.assign({}), std::invalid_argument);
    BOOST_TEST(storage.getStorage().data() == address);
    BOOST_TEST(storage.getStorage()[0].getIntegerValue() == 7);
}

BOOST_AUTO_TEST_CASE(failed_copies_release_partial_ownership_and_preserve_assignment_destinations) {
    Runtime runtime;
    FailingStorageCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<FailingStorageCopyType>(
        "MutableStorageCopy", ClassMode::Closed, field);
    BOOST_REQUIRE(fieldId.isValid());
    const auto pairId = runtime.registerStruct("MutableStoragePair");
    const auto* pair = runtime.getType(pairId).value();
    BOOST_REQUIRE(runtime.registerProperty(pairId, "first", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "second", field).isValid());
    runtime.finalizeCompositeFieldLayout(pairId);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    std::array<RuntimeValue, 2> original{
        RuntimeValue{NomadString{"old first"}}, RuntimeValue{NomadString{"old second"}}
    };
    std::array<RuntimeValue, 2> replacement{
        RuntimeValue{NomadString{"new first"}}, RuntimeValue{NomadString{"new second"}}
    };
    const auto live = RuntimeValue::getLiveStringCount();
    for (NomadIndex failAt = 1; failAt <= 2; ++failAt) {
        field->copyCount = 0;
        field->failCopyAt = failAt;
        BOOST_CHECK_THROW((InterpreterStorage{pair, original}), NomadException);
        BOOST_TEST(original[0].getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(original[1].getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
    }
    field->failCopyAt = 0;
    {
        InterpreterStorage storage(pair, original);
        const auto* address = storage.getStorage().data();
        for (NomadIndex failAt = 1; failAt <= 2; ++failAt) {
            field->copyCount = 0;
            field->failCopyAt = failAt;
            BOOST_CHECK_THROW(storage.assign(replacement), NomadException);
            BOOST_TEST(storage.getStorage().data() == address);
            BOOST_TEST(storage.getStorage()[0].getStringValue() == "old first");
            BOOST_TEST(storage.getStorage()[1].getStringValue() == "old second");
            BOOST_TEST(original[0].getObjectValue()->getReferenceCount() == 2U);
            BOOST_TEST(original[1].getObjectValue()->getReferenceCount() == 2U);
            BOOST_TEST(replacement[0].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(replacement[1].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
        }
        field->copyCount = 0;
        field->failCopyAt = 0;
        storage.assign(replacement);
        BOOST_TEST(field->copyCount == 2U);
        BOOST_TEST(storage.getStorage()[0].getStringValue() == "new first");
        BOOST_TEST(storage.getStorage()[1].getStringValue() == "new second");
    }
    pair->freeValue(original.data());
    pair->freeValue(replacement.data());
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(context_growth_and_same_type_updates_preserve_addresses_without_aliasing_reads) {
    Runtime runtime;
    const auto ownerId = runtime.registerStruct("ContextStorageStruct");
    const auto* owner = runtime.getType(ownerId).value();
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "number", runtime.getIntegerType()).isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    const auto emptyId = runtime.registerStruct("ContextStorageEmpty");
    const auto* empty = runtime.getType(emptyId).value();
    runtime.finalizeCompositeFieldLayout(emptyId);
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    std::array<RuntimeValue, 1> source{RuntimeValue{NomadInteger{17}}};
    context.setVariable("value", owner, source);
    const auto* address = context.getVariableStorage("value").data();
    const auto executed = interpreter.execute("copy = value", context);
    BOOST_REQUIRE_MESSAGE(executed, (context.getError() != nullptr ? *context.getError() : "No error reported"));
    const InterpreterValue snapshot(*context.getResult());
    for (auto index = 0; index < 128; ++index) {
        context.setVariable("extra" + std::to_string(index), runtime.getIntegerType(),
            RuntimeValue{NomadInteger{index}});
    }
    BOOST_TEST(context.getVariableStorage("value").data() == address);
    source[0].setIntegerValue(41);
    context.setVariable("value", owner, source);
    BOOST_TEST(context.getVariableStorage("value").data() == address);
    BOOST_TEST(context.getVariableStorage("value")[0].getIntegerValue() == 41);
    BOOST_TEST(context.getVariableStorage("copy")[0].getIntegerValue() == 17);
    BOOST_TEST(snapshot.getStorage()[0].getIntegerValue() == 17);
    context.setVariable("value", owner, context.getVariableStorage("value"));
    BOOST_TEST(context.getVariableStorage("value").data() == address);
    context.setVariable("empty", empty, std::span<const RuntimeValue>{});
    context.setVariable("empty", empty, std::span<const RuntimeValue>{});
    BOOST_TEST(context.getVariableType("empty") == empty);
    BOOST_TEST(context.getVariableStorage("empty").data() == nullptr);
    context.clearVariableValue("empty");
    BOOST_TEST(context.getVariableType("empty") == nullptr);
    BOOST_TEST(context.getVariableStorage("empty").empty());
    context.setVariable("value", runtime.getIntegerType(), RuntimeValue{NomadInteger{29}});
    BOOST_TEST(context.getVariableType("value") == runtime.getIntegerType());
    BOOST_TEST(context.getVariableValue("value")->getIntegerValue() == 29);
    BOOST_TEST(snapshot.getStorage()[0].getIntegerValue() == 17);
}

BOOST_AUTO_TEST_CASE(failed_context_type_replacement_preserves_existing_storage_and_metadata) {
    Runtime runtime;
    FailingStorageCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<FailingStorageCopyType>(
        "ContextStorageCopy", ClassMode::Closed, field);
    BOOST_REQUIRE(fieldId.isValid());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    InterpreterContext context(interpreter);
    context.setVariable("value", runtime.getIntegerType(), RuntimeValue{NomadInteger{17}});
    const auto* address = context.getVariableStorage("value").data();
    RuntimeValue source{NomadString{"replacement"}};
    field->copyCount = 0;
    field->failCopyAt = 1;
    BOOST_CHECK_THROW(context.setVariable("value", field, source), NomadException);
    BOOST_TEST(context.getVariableStorage("value").data() == address);
    BOOST_TEST(context.getVariableType("value") == runtime.getIntegerType());
    BOOST_TEST(context.getCallable()->getVariableType(
        context.getCallable()->getVariableId("value")) == runtime.getIntegerType());
    BOOST_TEST(context.getVariableValue("value")->getIntegerValue() == 17);
    BOOST_TEST(source.getObjectValue()->getReferenceCount() == 1U);
    field->failCopyAt = 0;
    context.setVariable("value", field, source);
    BOOST_TEST(context.getVariableType("value") == field);
    BOOST_TEST(source.getObjectValue()->getReferenceCount() == 2U);
    context.clearVariableValue("value");
    BOOST_TEST(source.getObjectValue()->getReferenceCount() == 1U);
    source.freeObjectValue();
}

BOOST_AUTO_TEST_SUITE_END()
