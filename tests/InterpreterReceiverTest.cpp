// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/interpreter/Interpreter.hpp>
#include <nomad/interpreter/InterpreterObjectReceiver.hpp>
#include <nomad/interpreter/InterpreterStorage.hpp>
#include <nomad/interpreter/InterpreterStructReceiver.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace nomad;

namespace {

class FailingCompositeCopyType final : public ClassType {
public:
    FailingCompositeCopyType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        FailingCompositeCopyType*& descriptor
    ):
        ClassType(runtime, id, std::move(name), mode)
    {
        descriptor = this;
    }

    void copyValue(const RuntimeValue* source, RuntimeValue* destination) const override {
        ClassType::copyValue(source, destination);
        if (++copyCount == failCopyAt) {
            throw NomadException("Composite copy failed");
        }
    }

    mutable NomadIndex copyCount = 0;
    NomadIndex failCopyAt = 0;
};

} // namespace

BOOST_AUTO_TEST_SUITE(interpreter_receivers)

BOOST_AUTO_TEST_CASE(object_factory_retains_identity_after_the_source_value_is_destroyed) {
    Runtime runtime;
    const auto ownerId = runtime.registerClass("FactoryObject", ClassMode::Closed);
    const auto* owner = runtime.getType(ownerId).value()->asClassType();
    const auto numberProperty = runtime.registerProperty(ownerId, "number", runtime.getIntegerType());
    BOOST_REQUIRE(numberProperty.isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    {
        std::unique_ptr<InterpreterObjectReceiver> receiver;
        {
            RuntimeValue value;
            value.setObjectValue(new ScriptObject(owner));
            const InterpreterValue source(owner, value);
            value.freeObjectValue();
            receiver = interpreter.createObjectReceiver(*owner, source.getValue().getObjectValue());
            BOOST_TEST(receiver->getType() == owner);
            BOOST_TEST(source.getValue().getObjectValue()->getReferenceCount() == 2U);
        }
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore + 1);
        const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{41}});
        receiver->setProperty(numberProperty, number);
        BOOST_TEST(receiver->getProperty(numberProperty).getValue().getIntegerValue() == 41);
        const auto snapshot = receiver->getValue();
        BOOST_TEST(snapshot.getValue().getObjectValue()->getReferenceCount() == 2U);
        receiver.reset();
        BOOST_TEST(snapshot.getValue().getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(snapshot.getValue().asObject<ScriptObject>()->getField(
            owner->getFieldIndex(numberProperty)).getIntegerValue() == 41);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(object_factory_supports_string_class_receivers_and_releases_its_reference) {
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto* type = runtime.getStringType()->asClassType();
    BOOST_REQUIRE(type != nullptr);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    {
        auto receiver = [&]() {
            RuntimeValue value{NomadString{"retained"}};
            const InterpreterValue source(type, value);
            value.freeObjectValue();
            auto wrapper = interpreter.createObjectReceiver(*type, source.getValue().getObjectValue());
            BOOST_TEST(source.getValue().getObjectValue()->getReferenceCount() == 2U);
            return wrapper;
        }();
        BOOST_TEST(receiver->getType() == type);
        BOOST_TEST(receiver->getProperty("length").getValue().getIntegerValue() == 8);
        {
            const auto snapshot = receiver->getValue();
            BOOST_TEST(snapshot.getValue().getStringValue() == "retained");
            BOOST_TEST(snapshot.getValue().getObjectValue()->getReferenceCount() == 2U);
        }
        receiver.reset();
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(struct_factory_borrows_storage_without_copying_or_releasing_it) {
    Runtime runtime;
    const auto ownerId = runtime.registerStruct("FactoryStruct");
    const auto* owner = runtime.getType(ownerId).value()->asStructType();
    const auto numberProperty = runtime.registerProperty(ownerId, "number", runtime.getIntegerType());
    BOOST_REQUIRE(numberProperty.isValid());
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    std::array<RuntimeValue, 2> storage{
        RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadString{"borrowed"}}
    };
    {
        const auto receiver = interpreter.createStructReceiver(*owner, storage.data());
        BOOST_TEST(receiver->getType() == owner);
        BOOST_TEST(storage[1].getObjectValue()->getReferenceCount() == 1U);
        const auto snapshot = receiver->getValue();
        storage[0].setIntegerValue(29);
        BOOST_TEST(receiver->getProperty(numberProperty).getValue().getIntegerValue() == 29);
        const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{41}});
        receiver->setProperty(numberProperty, number);
        BOOST_TEST(storage[0].getIntegerValue() == 41);
        BOOST_TEST(snapshot.getStorage()[0].getIntegerValue() == 17);
        BOOST_TEST(snapshot.getStorage().data() != storage.data());
    }
    BOOST_TEST(storage[0].getIntegerValue() == 41);
    BOOST_TEST(storage[1].getStringValue() == "borrowed");
    BOOST_TEST(storage[1].getObjectValue()->getReferenceCount() == 1U);
    BOOST_CHECK_THROW(
        static_cast<void>(interpreter.createStructReceiver(*owner, nullptr)), std::invalid_argument);
    owner->freeValue(storage.data());
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(struct_factory_supports_null_storage_for_zero_sized_structs_and_fields) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("FactoryEmpty");
    const auto* empty = runtime.getType(emptyId).value()->asStructType();
    const auto ownerId = runtime.registerStruct("FactoryEmptyOwner");
    const auto* owner = runtime.getType(ownerId).value()->asStructType();
    const auto property = runtime.registerProperty(ownerId, "empty", empty);
    BOOST_REQUIRE(property.isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    BOOST_TEST(empty->getSize() == 0U);
    BOOST_TEST(owner->getSize() == 0U);
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto emptyReceiver = interpreter.createStructReceiver(*empty, nullptr);
    const auto emptyValue = emptyReceiver->getValue();
    BOOST_TEST(emptyValue.getType() == empty);
    BOOST_TEST(emptyValue.getStorage().empty());
    const auto ownerReceiver = interpreter.createStructReceiver(*owner, nullptr);
    ownerReceiver->setProperty(property, emptyValue);
    BOOST_TEST(ownerReceiver->getProperty(property).getType() == empty);
    BOOST_TEST(ownerReceiver->getProperty("empty").getStorage().empty());
    const auto snapshot = ownerReceiver->getValue();
    BOOST_TEST(snapshot.getType() == owner);
    BOOST_TEST(snapshot.getStorage().data() == nullptr);
}

BOOST_AUTO_TEST_CASE(struct_wrappers_borrow_original_or_caller_owned_storage_and_support_empty_values) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("CompositeEmpty");
    const auto* empty = runtime.getTypeByName("CompositeEmpty")->asStructType();
    const auto leafId = runtime.registerStruct("CompositeLeaf");
    const auto* leaf = runtime.getTypeByName("CompositeLeaf")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(leafId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(leafId, "text", runtime.getStringType()).isValid());
    const auto rootId = runtime.registerStruct("CompositeRoot");
    const auto* root = runtime.getTypeByName("CompositeRoot")->asStructType();
    const auto leafProperty = runtime.registerProperty(rootId, "leaf", leaf);
    const auto emptyProperty = runtime.registerProperty(rootId, "empty", empty);
    BOOST_REQUIRE(leafProperty.isValid() && emptyProperty.isValid());
    runtime.finalizeCompositeFieldLayout(rootId);
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    {
        std::array<RuntimeValue, 2> storage{
            RuntimeValue{NomadInteger{17}}, RuntimeValue{NomadString{"borrowed"}}
        };
        {
            InterpreterStructReceiver borrowed(interpreter, *root, storage.data());
            const auto original = borrowed.getValue();
            InterpreterStorage copiedStorage(root, original.getStorage());
            InterpreterStructReceiver copied(interpreter, *root, copiedStorage.getMutableStorage().data());
            const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{29}});
            borrowed.setProperty("leaf.number", number);
            BOOST_TEST(storage[0].getIntegerValue() == 29);
            BOOST_TEST(original.getStorage()[0].getIntegerValue() == 17);
            BOOST_TEST(copied.getProperty("leaf.number").getValue().getIntegerValue() == 17);
            const auto snapshot = borrowed.getValue();
            BOOST_TEST(snapshot.getStorage().data() != storage.data());
            RuntimeValue textSource{NomadString{"copied"}};
            const InterpreterValue text(runtime.getStringType(), textSource);
            textSource.freeObjectValue();
            copied.setProperty("leaf.text", text);
            BOOST_TEST(copied.getProperty("leaf.text").getValue().getStringValue() == "copied");
            BOOST_TEST(storage[1].getStringValue() == "borrowed");
            BOOST_TEST(snapshot.getStorage()[1].getStringValue() == "borrowed");
            InterpreterStructReceiver borrowedEmpty(interpreter, *empty, nullptr);
            const auto emptyValue = borrowedEmpty.getValue();
            InterpreterStorage emptyStorage(empty, emptyValue.getStorage());
            InterpreterStructReceiver copiedEmpty(interpreter, *empty, emptyStorage.getMutableStorage().data());
            BOOST_TEST(emptyValue.getType() == empty);
            BOOST_TEST(copiedEmpty.getValue().getStorage().empty());
            borrowed.setProperty(emptyProperty, emptyValue);
            BOOST_TEST(borrowed.getProperty(emptyProperty).getType() == empty);
            BOOST_TEST(borrowed.getProperty(leafProperty).getStorage()[0].getIntegerValue() == 29);
            BOOST_CHECK_THROW(static_cast<void>(borrowed.getProperty("leaf.")), std::invalid_argument);
            BOOST_CHECK_THROW(static_cast<void>(borrowed.getProperty("missing")), std::invalid_argument);
            BOOST_CHECK_THROW((InterpreterStructReceiver{interpreter, *root, nullptr}), std::invalid_argument);
        }
        BOOST_TEST(storage[1].getObjectValue()->getReferenceCount() == 1U);
        root->freeValue(storage.data());
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    InterpreterContext console(interpreter);
    BOOST_TEST(console.getReceiver() == nullptr);
    BOOST_CHECK_THROW(console.setReceiverProperty(leafProperty,
        InterpreterValue(empty, std::span<const RuntimeValue>{})), NomadBug);
}

BOOST_AUTO_TEST_CASE(object_wrappers_share_identity_and_support_stored_and_native_properties) {
    Runtime runtime;
    const auto leafId = runtime.registerStruct("ObjectCompositeLeaf");
    const auto* leaf = runtime.getTypeByName("ObjectCompositeLeaf")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(leafId, "number", runtime.getIntegerType()).isValid());
    runtime.finalizeCompositeFieldLayout(leafId);
    const auto ownerId = runtime.registerClass<ClassType>("ObjectCompositeOwner", ClassMode::Closed);
    const auto* owner = runtime.getType(ownerId).value()->asClassType();
    const auto numberProperty = runtime.registerProperty(ownerId, "number", runtime.getIntegerType());
    const auto leafProperty = runtime.registerProperty(ownerId, "leaf", leaf);
    BOOST_REQUIRE(numberProperty.isValid() && leafProperty.isValid());
    const auto nativeProperty = runtime.addProperty(ownerId, {
        .valueType = runtime.getIntegerType(), .name = "nativeNumber",
        .getter = [owner, numberProperty](VirtualMachine* active, RuntimeValue& result) {
            result = active->getThisAs<ScriptObject>()->getField(owner->getFieldIndex(numberProperty));
        },
        .setter = [owner, numberProperty](VirtualMachine* active, const RuntimeValue& value) {
            active->getThisAs<ScriptObject>()->setField(owner->getFieldIndex(numberProperty), value);
        }
    });
    const auto failureProperty = runtime.addProperty(ownerId, {
        .valueType = runtime.getStringType(), .name = "fail",
        .getter = [](VirtualMachine*, RuntimeValue& result) {
            result.setStringValue("partial property value");
            throw NomadException("Expected property failure");
        }
    });
    BOOST_REQUIRE(nativeProperty.isValid() && failureProperty.isValid());
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    auto* object = new ScriptObject(owner);
    {
        InterpreterObjectReceiver wrapper(interpreter, *owner, object);
        BOOST_TEST(object->getReferenceCount() == 2U);
        const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{41}});
        wrapper.setProperty(numberProperty, number);
        BOOST_TEST(wrapper.getProperty(numberProperty).getValue().getIntegerValue() == 41);
        {
            const auto snapshot = wrapper.getValue();
            BOOST_TEST(snapshot.getValue().getObjectValue() == object);
            BOOST_TEST(object->getReferenceCount() == 3U);
            wrapper.setProperty(nativeProperty, number);
            BOOST_TEST(snapshot.getValue().asObject<ScriptObject>()->getField(
                owner->getFieldIndex(numberProperty)).getIntegerValue() == 41);
        }
        wrapper.setProperty("leaf.number", number);
        BOOST_TEST(wrapper.getProperty("leaf.number").getValue().getIntegerValue() == 41);
        BOOST_TEST(wrapper.getProperty(leafProperty).getType() == leaf);
        BOOST_TEST(wrapper.getProperty("nativeNumber").getValue().getIntegerValue() == 41);
        BOOST_CHECK_THROW(static_cast<void>(wrapper.getProperty(failureProperty)), NomadException);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
    }
    BOOST_TEST(object->getReferenceCount() == 1U);
    object->releaseRef();
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(struct_property_copy_failures_preserve_original_and_caller_owned_storage) {
    Runtime runtime;
    FailingCompositeCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<FailingCompositeCopyType>("CompositeCopy", ClassMode::Closed, field);
    BOOST_REQUIRE(fieldId.isValid());
    const auto pairId = runtime.registerStruct("CompositeCopyPair");
    const auto* pair = runtime.getTypeByName("CompositeCopyPair")->asStructType();
    const auto firstProperty = runtime.registerProperty(pairId, "first", field);
    BOOST_REQUIRE(firstProperty.isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "second", field).isValid());
    runtime.finalizeCompositeFieldLayout(pairId);
    auto machine = runtime.createVirtualMachine();
    const Interpreter interpreter(*machine);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    for (const auto owned : {false, true}) {
        std::array<RuntimeValue, 2> storage{
            RuntimeValue{NomadString{"old first"}}, RuntimeValue{NomadString{"old second"}}
        };
        {
            RuntimeValue source{NomadString{"replacement"}};
            const InterpreterValue replacement(field, source);
            source.freeObjectValue();
            std::optional<InterpreterStorage> copiedStorage;
            const auto create = [&]() {
                if (owned) {
                    copiedStorage.emplace(pair, storage);
                    return interpreter.createStructReceiver(*pair, copiedStorage->getMutableStorage().data());
                }
                return interpreter.createStructReceiver(*pair, storage.data());
            };
            field->failCopyAt = 0;
            auto successful = create();
            field->copyCount = 0;
            successful->setProperty(firstProperty, replacement);
            const auto copyStages = field->copyCount;
            successful.reset();
            copiedStorage.reset();
            storage[0].freeObjectValue();
            storage[0].setStringValue("old first");
            BOOST_REQUIRE(copyStages > 0);
            for (NomadIndex failAt = 1; failAt <= copyStages; ++failAt) {
                field->failCopyAt = 0;
                auto receiver = create();
                const auto live = RuntimeValue::getLiveStringCount();
                field->copyCount = 0;
                field->failCopyAt = failAt;
                BOOST_CHECK_THROW(receiver->setProperty(firstProperty, replacement), NomadException);
                field->failCopyAt = 0;
                {
                    const auto snapshot = receiver->getValue();
                    BOOST_TEST(snapshot.getStorage()[0].getStringValue() == "old first");
                    BOOST_TEST(snapshot.getStorage()[1].getStringValue() == "old second");
                }
                BOOST_TEST(replacement.getValue().getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(storage[0].getObjectValue()->getReferenceCount() == (owned ? 2U : 1U));
                BOOST_TEST(storage[1].getObjectValue()->getReferenceCount() == (owned ? 2U : 1U));
                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            }
        }
        pair->freeValue(storage.data());
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(frame_context_receivers_survive_native_receiver_overrides_and_validate_frame_access) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto visits = 0;
    const auto inspectFrameId = runtime.registerNativeFunction(
        "inspectCompositeFrame",
        [&](VirtualMachine* active) {
            ++visits;
            const auto* callable = active->getCurrentCallable();
            BOOST_REQUIRE(callable != nullptr);
            const Interpreter interpreter(*active);
            const auto state = active->captureExecutionState();
            ScriptObject* foreign = nullptr;
            NomadObject* original = nullptr;
            const auto* type = callable->getClassType();
            if (type != nullptr && !type->isStruct()) {
                original = active->getScriptReceiver();
                foreign = new ScriptObject(type->asClassType());
                RuntimeValue foreignReceiver;
                foreignReceiver.setObjectValue(foreign);
                active->enterNativeFrame(foreignReceiver);
            }
            {
                InterpreterContext context(interpreter, *callable);
                const auto* receiver = std::as_const(context).getReceiver();
                if (type == nullptr) {
                    BOOST_TEST(receiver == nullptr);
                } else {
                    BOOST_REQUIRE(receiver != nullptr);
                    BOOST_TEST(receiver->getType() == type);
                    if (type->getSize() == 0) {
                        BOOST_TEST(dynamic_cast<const InterpreterStructReceiver*>(receiver) != nullptr);
                        BOOST_TEST(receiver->getValue().getStorage().empty());
                        BOOST_REQUIRE(interpreter.execute("this", context));
                        BOOST_TEST(context.getResult()->getType() == type);
                    } else {
                        const auto before = receiver->getValue();
                        const auto numberBefore = receiver->getProperty("number").getValue().getIntegerValue();
                        if (type->isStruct()) {
                            BOOST_TEST(dynamic_cast<const InterpreterStructReceiver*>(receiver) != nullptr);
                        } else {
                            BOOST_TEST(dynamic_cast<const InterpreterObjectReceiver*>(receiver) != nullptr);
                            BOOST_TEST(before.getValue().getObjectValue() == original);
                            BOOST_TEST(active->getThis() == foreign);
                        }
                        BOOST_REQUIRE(interpreter.execute("this.number = 29\nthis.number", context));
                        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 29);
                        const InterpreterValue changed(runtime.getIntegerType(), RuntimeValue{NomadInteger{31}});
                        context.setReceiverProperty("number", changed);
                        BOOST_TEST(receiver->getProperty("number").getValue().getIntegerValue() == 31);
                        if (type->isStruct()) {
                            BOOST_TEST(before.getStorage()[0].getIntegerValue() == numberBefore);
                            BOOST_TEST(context.getReceiverStorage()[0].getIntegerValue() == 31);
                        } else {
                            BOOST_TEST(before.getValue().asObject<ScriptObject>()->getField(0).getIntegerValue() == 31);
                            BOOST_TEST(active->getThis() == foreign);
                        }
                    }
                }
                const auto current = active->captureExecutionState();
                const auto* metadata = context.getCallable();
                BOOST_TEST(std::as_const(context).getCallable() == metadata);
                auto invalid = current;
                ++invalid.parameterIndex;
                active->restoreExecutionState(invalid);
                BOOST_CHECK_THROW(static_cast<void>(context.getReceiver()), NomadBug);
                BOOST_CHECK_THROW(static_cast<void>(std::as_const(context).getReceiver()), NomadBug);
                BOOST_CHECK_THROW(static_cast<void>(context.getMutableReceiverStorage()), NomadBug);
                BOOST_CHECK_THROW(static_cast<void>(context.getCallable()), NomadBug);
                BOOST_CHECK_THROW(static_cast<void>(std::as_const(context).getCallable()), NomadBug);
                BOOST_TEST(!interpreter.execute("0", context));
                BOOST_REQUIRE(context.getError() != nullptr);
                BOOST_TEST(context.getDiagnostics()[0].sourceName == metadata->getPath());
                BOOST_TEST(context.getResult() == nullptr);
                active->restoreExecutionState(current);
            }
            active->restoreExecutionState(state);
            if (foreign != nullptr) {
                foreign->releaseRef();
            }
        },
        {},
        runtime.getVoidType(),
        ""
    );
    BOOST_REQUIRE(inspectFrameId.isValid());
    fixture.addFunction("compositeFrameTypes",
        "class CompositeFrameObject\nnumber = 7\n"
        "init number:int\nthis.number = number\ninspectCompositeFrame\nend\n"
        "fun inspect\ninspectCompositeFrame\nreturn this.number\nend\nend\n"
        "struct CompositeFrameStruct\nnumber = 7\n"
        "init number:int\nthis.number = number\ninspectCompositeFrame\nend\n"
        "fun inspect\ninspectCompositeFrame\nreturn this.number\nend\nend\n"
        "struct CompositeFrameEmpty\ninit\ninspectCompositeFrame\nend\nend\nreturn 0");
    fixture.addFunction("compositeFreeFrame", "inspectCompositeFrame\nreturn 0");
    fixture.addFunction("compositeFrameMain",
        "object = new CompositeFrameObject 17\nassert (object.inspect) == 31\n"
        "value = new CompositeFrameStruct 17\nassert (value.inspect) == 31\n"
        "empty = new CompositeFrameEmpty\nreturn compositeFreeFrame");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    const auto outcome = fixture.execute("compositeFrameMain");
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.integerValue == 0);
    BOOST_TEST(outcome.leakedStrings == 0);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    BOOST_TEST(visits == 6);
}

BOOST_AUTO_TEST_CASE(method_backed_struct_properties_return_snapshots_and_invoke_whole_value_setters) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto sharedId = runtime.registerClass("AccessorMethodShared", ClassMode::Closed);
    const auto* shared = runtime.getTypeByName("AccessorMethodShared")->asClassType();
    BOOST_REQUIRE(runtime.registerProperty(sharedId, "number", runtime.getIntegerType()).isValid());
    runtime.finalizeCompositeFieldLayout(sharedId);
    const auto valueId = runtime.registerStruct("AccessorMethodValue");
    const auto* type = runtime.getTypeByName("AccessorMethodValue")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(valueId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "shared", shared).isValid());
    const auto emptyId = runtime.registerStruct("AccessorMethodEmpty");
    const auto* emptyType = runtime.getTypeByName("AccessorMethodEmpty");
    const auto ownerId = runtime.registerClass("AccessorMethodOwner", ClassMode::Closed);
    const auto* owner = runtime.getTypeByName("AccessorMethodOwner")->asClassType();
    const auto backingId = runtime.registerProperty(ownerId, "backing", type);
    BOOST_REQUIRE(backingId.isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    runtime.finalizeCompositeFieldLayout(emptyId);
    auto getterCalls = 0;
    auto setterCalls = 0;
    auto emptyGetterCalls = 0;
    auto emptySetterCalls = 0;
    const auto numberMethod = runtime.addMethod(ownerId, {
        .name = "readNumber", .parameters = {}, .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [](VirtualMachine* active) {
            active->setIntegerResult(static_cast<ScriptObject*>(active->getThis())->getFieldStorage(0)[0].getIntegerValue());
        }
    });
    const auto getter = runtime.addMethod(ownerId, {
        .name = "getValue", .parameters = {}, .returnTypeId = valueId,
        .function = [&](VirtualMachine* active) {
            ++getterCalls;
            auto* object = static_cast<ScriptObject*>(active->getThis());
            auto* destination = &active->getStorageParameter(0, 0);
            const Interpreter nested(*active);
            const auto number = nested.runObjectMethod(*owner, object, numberMethod, {});
            BOOST_TEST(number.getValue().getIntegerValue() == object->getFieldStorage(0)[0].getIntegerValue());
            BOOST_TEST(active->getThis() == object);
            BOOST_TEST(&active->getStorageParameter(0, 0) == destination);
            type->copyValue(object->getFieldStorage(0).data(), destination);
        }
    });
    const auto setter = runtime.addMethod(ownerId, {
        .name = "setValue", .parameters = {{valueId, "value", ""}},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [&](VirtualMachine* active) {
            ++setterCalls;
            auto* object = static_cast<ScriptObject*>(active->getThis());
            const auto argument = active->getParameterStorage(0, type->getSize());
            const Interpreter nested(*active);
            const auto previous = nested.runObjectMethod(*owner, object, getter, {});
            BOOST_TEST(previous.getStorage()[0].getIntegerValue() == object->getFieldStorage(0)[0].getIntegerValue());
            BOOST_TEST(active->getThis() == object);
            BOOST_TEST(active->getParameterStorage(0, type->getSize()).data() == argument.data());
            std::vector<RuntimeValue> scratch(type->getAssignmentScratchSize());
            object->setFieldStorage(0, argument, scratch);
            argument[0].setIntegerValue(99);
        }
    });
    const auto emptyGetter = runtime.addMethod(ownerId, {
        .name = "getEmpty", .parameters = {}, .returnTypeId = emptyId,
        .function = [&](VirtualMachine*) { ++emptyGetterCalls; }
    });
    const auto emptySetter = runtime.addMethod(ownerId, {
        .name = "setEmpty", .parameters = {{emptyId, "empty", ""}},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [&](VirtualMachine*) { ++emptySetterCalls; }
    });
    BOOST_REQUIRE(numberMethod.isValid() && getter.isValid() && setter.isValid() &&
        emptyGetter.isValid() && emptySetter.isValid());
    const auto propertyId = runtime.addProperty(ownerId, {
        .valueType = type, .name = "value", .getterMethodId = getter, .setterMethodId = setter
    });
    const auto readOnlyId = runtime.addProperty(ownerId, {
        .valueType = type, .name = "readOnly", .getterMethodId = getter
    });
    const auto writeOnlyId = runtime.addProperty(ownerId, {
        .valueType = type, .name = "writeOnly", .setterMethodId = setter
    });
    const auto emptyProperty = runtime.addProperty(ownerId, {
        .valueType = emptyType, .name = "empty", .getterMethodId = emptyGetter, .setterMethodId = emptySetter
    });
    const auto numberProperty = runtime.addProperty(ownerId, {
        .valueType = runtime.getIntegerType(), .name = "number", .getterMethodId = numberMethod
    });
    BOOST_REQUIRE(propertyId.isValid() && readOnlyId.isValid() && writeOnlyId.isValid() &&
        emptyProperty.isValid() && numberProperty.isValid());
    BOOST_REQUIRE(runtime.addMethod(valueId, {
        .name = "bump", .parameters = {}, .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [](VirtualMachine* active) {
            auto& number = active->getStorageParameter(0, 0);
            number.setIntegerValue(number.getIntegerValue() + 1);
            active->setIntegerResult(number.getIntegerValue());
        }
    }).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("makeAccessorMethodOwner", [owner](VirtualMachine* active) {
        active->getResult().setObjectValue(new ScriptObject(owner));
    }, {}, owner, "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("makeAccessorMethodValue", [shared](VirtualMachine* active) {
        auto* destination = &active->getStorageParameter(0, 0);
        destination[0].setIntegerValue(7);
        destination[1].setStringValue("accessor argument");
        destination[2].setObjectValue(new ScriptObject(shared));
    }, {}, type, "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("makeAccessorMethodEmpty", [](VirtualMachine*) {},
        {}, emptyType, "").isValid());
    fixture.addFunction("accessorMethodTypes", "return 0");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        {
            std::optional<InterpreterValue> retained;
            {
                auto machine = runtime.createVirtualMachine();
                const Interpreter interpreter(*machine);
                InterpreterContext context(interpreter);
                const auto execute = [&](const NomadString& source) {
                    BOOST_REQUIRE_MESSAGE(interpreter.execute(source, context),
                        (context.getError() ? *context.getError() : source));
                };
                execute("owner = makeAccessorMethodOwner\nsource = makeAccessorMethodValue\nowner.value = source\ncopy = owner.value");
                const auto wrapper = interpreter.createObjectReceiver(*owner, context.getVariableValue("owner")->getObjectValue());
                retained = wrapper->getProperty(propertyId);
                const auto settersBeforeMutation = setterCalls;
                const auto gettersBeforeMutation = getterCalls;
                execute("owner.value.number = 31\nowner.value.bump\nowner.readOnly.number = 53\nowner.readOnly.bump\nowner.value.number");
                BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 7);
                BOOST_TEST(getterCalls == gettersBeforeMutation + 5);
                BOOST_TEST(setterCalls == settersBeforeMutation);
                execute("copy.shared.number = 29\nowner.value.shared.number");
                BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 29);
                const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{41}});
                wrapper->setProperty("value.number", number);
                BOOST_TEST(wrapper->getProperty(numberProperty).getValue().getIntegerValue() == 7);
                BOOST_TEST(setterCalls == settersBeforeMutation);
                execute("copy.number");
                BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 7);
                execute("source.number");
                BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 7);
                execute("source.number = 17\nowner.writeOnly = source\nowner.value.number");
                BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 17);
                BOOST_TEST(retained->getStorage()[0].getIntegerValue() == 7);
                BOOST_TEST(!interpreter.execute("owner.readOnly = source", context));
                BOOST_REQUIRE(context.getError() != nullptr);
                BOOST_TEST(context.getError()->find("read-only") != NomadString::npos);
                BOOST_TEST(!interpreter.execute("owner.writeOnly", context));
                BOOST_REQUIRE(context.getError() != nullptr);
                BOOST_TEST(context.getError()->find("write-only") != NomadString::npos);
                BOOST_CHECK_THROW(wrapper->setProperty(readOnlyId, *retained), std::invalid_argument);
                BOOST_CHECK_THROW(static_cast<void>(wrapper->getProperty(writeOnlyId)), std::invalid_argument);
                BOOST_CHECK_THROW(wrapper->setProperty(propertyId, number), std::invalid_argument);
                execute("owner.empty = makeAccessorMethodEmpty\nowner.empty");
                BOOST_TEST(context.getResult()->getType() == emptyType);
                BOOST_TEST(context.getResult()->getStorage().data() == nullptr);
                execute("(makeAccessorMethodOwner).value.number");
                BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 0);
                RuntimeValue receiver;
                receiver.setObjectValue(context.getVariableValue("owner")->getObjectValue());
                RuntimeValue result;
                BOOST_CHECK_EXCEPTION(machine->getNativeProperty(propertyId, receiver, result), VirtualMachineException,
                    [](const auto& error) {
                        return NomadString{error.what()} == "Method-backed native properties require typed interpreter access";
                    });
            }
            BOOST_TEST(retained->getStorage()[0].getIntegerValue() == 7);
            BOOST_TEST(retained->getStorage()[1].getStringValue() == "accessor argument");
            BOOST_TEST(retained->getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(retained->getStorage()[2].getObjectValue()->getReferenceCount() == 1U);
        }
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(getterCalls > 0);
    BOOST_TEST(setterCalls == 6);
    BOOST_TEST(emptyGetterCalls == 3);
    BOOST_TEST(emptySetterCalls == 3);
}

BOOST_AUTO_TEST_CASE(method_backed_property_failures_clean_partial_values_and_preserve_callback_mutations) {
    Runtime runtime;
    FailingCompositeCopyType* field = nullptr;
    const auto fieldId = runtime.registerClass<FailingCompositeCopyType>("MethodAccessorCopy", ClassMode::Closed, field);
    const auto valueId = runtime.registerStruct("MethodAccessorPair");
    const auto* type = runtime.getTypeByName("MethodAccessorPair");
    BOOST_REQUIRE(fieldId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "first", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(valueId, "second", field).isValid());
    const auto ownerId = runtime.registerClass("MethodAccessorFailureOwner", ClassMode::Closed);
    const auto* owner = runtime.getTypeByName("MethodAccessorFailureOwner")->asClassType();
    const auto backingId = runtime.registerProperty(ownerId, "backing", type);
    BOOST_REQUIRE(backingId.isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    auto failGetter = false;
    auto failSetter = false;
    const auto getter = runtime.addMethod(ownerId, {
        .name = "getValue", .parameters = {}, .returnTypeId = valueId,
        .function = [&](VirtualMachine* active) {
            auto* destination = &active->getStorageParameter(0, 0);
            if (failGetter) {
                destination[0].setStringValue("partial accessor result");
                throw NomadException("Accessor getter failed");
            }
            type->copyValue(static_cast<ScriptObject*>(active->getThis())->getFieldStorage(0).data(), destination);
        }
    });
    const auto setter = runtime.addMethod(ownerId, {
        .name = "setValue", .parameters = {{valueId, "value", ""}},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [&](VirtualMachine* active) {
            auto* object = static_cast<ScriptObject*>(active->getThis());
            std::vector<RuntimeValue> scratch(type->getAssignmentScratchSize());
            object->setFieldStorage(0, active->getParameterStorage(0, type->getSize()), scratch);
            if (failSetter) {
                throw NomadException("Accessor setter failed");
            }
        }
    });
    BOOST_REQUIRE(getter.isValid() && setter.isValid());
    const auto propertyId = runtime.addProperty(ownerId, {
        .valueType = type, .name = "value", .getterMethodId = getter, .setterMethodId = setter
    });
    BOOST_REQUIRE(propertyId.isValid());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        {
            auto machine = runtime.createVirtualMachine();
            const Interpreter interpreter(*machine);
            RuntimeValue object;
            object.setObjectValue(new ScriptObject(owner));
            const auto receiver = interpreter.createObjectReceiver(*owner, object.getObjectValue());
            object.freeObjectValue();
            std::array<RuntimeValue, 2> oldSource{RuntimeValue{NomadString{"old first"}}, RuntimeValue{NomadString{"old second"}}};
            std::array<RuntimeValue, 2> newSource{RuntimeValue{NomadString{"new first"}}, RuntimeValue{NomadString{"new second"}}};
            const InterpreterValue original(type, oldSource);
            const InterpreterValue replacement(type, newSource);
            type->freeValue(oldSource.data());
            type->freeValue(newSource.data());
            receiver->setProperty(backingId, original);
            machine->pushInteger(1234);
            machine->setIntegerResult(41);
            machine->setIntegerIntermediate(43);
            const auto live = RuntimeValue::getLiveStringCount();
            for (auto failure = NomadIndex{1}; failure <= 4; ++failure) {
                field->copyCount = 0;
                field->failCopyAt = failure;
                BOOST_CHECK_THROW(static_cast<void>(receiver->getProperty(propertyId)), NomadException);
                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
                BOOST_TEST(original.getStorage()[0].getObjectValue()->getReferenceCount() == 2U);
                field->copyCount = 0;
                BOOST_CHECK_THROW(receiver->setProperty(propertyId, replacement), NomadException);
                BOOST_TEST(original.getStorage()[0].getObjectValue()->getReferenceCount() == 2U);
                BOOST_TEST(replacement.getStorage()[0].getObjectValue()->getReferenceCount() == 1U);
                BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
                BOOST_TEST(machine->getIntegerResult() == 41);
                BOOST_TEST(machine->getIntegerIntermediate() == 43);
                BOOST_TEST(machine->peekStack(0).getIntegerValue() == 1234);
                BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
            }
            field->failCopyAt = 0;
            failGetter = true;
            BOOST_CHECK_THROW(static_cast<void>(receiver->getProperty(propertyId)), NomadException);
            BOOST_TEST(RuntimeValue::getLiveStringCount() == live);
            failGetter = false;
            InterpreterContext context(interpreter);
            context.setVariable("owner", owner, receiver->getValue().getStorage());
            context.setVariable("replacement", type, replacement.getStorage());
            failSetter = true;
            BOOST_TEST(!interpreter.execute("owner.value = replacement", context));
            BOOST_REQUIRE(context.getError() != nullptr);
            BOOST_TEST(context.getError()->find("Accessor setter failed") != NomadString::npos);
            BOOST_TEST(context.getResult() == nullptr);
            const auto changed = receiver->getProperty(backingId);
            BOOST_TEST(changed.getStorage()[0].getStringValue() == "new first");
            BOOST_TEST(replacement.getStorage()[0].getStringValue() == "new first");
            failSetter = false;
            machine->pop1();
        }
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(method_backed_properties_report_compiled_access_as_unsupported_until_the_next_slice) {
    for (const auto* source : {"owner.value", "owner.value = argument"}) {
        nomad::test::LanguageTestFixture fixture;
        auto& runtime = fixture.getRuntime();
        const auto valueId = runtime.registerStruct("PendingAccessorValue");
        const auto* type = runtime.getTypeByName("PendingAccessorValue");
        BOOST_REQUIRE(runtime.registerProperty(valueId, "number", runtime.getIntegerType()).isValid());
        const auto ownerId = runtime.registerClass("PendingAccessorOwner", ClassMode::Closed);
        const auto* owner = runtime.getTypeByName("PendingAccessorOwner")->asClassType();
        runtime.finalizeCompositeFieldLayout(valueId);
        const auto getter = runtime.addMethod(ownerId, {
            .name = "getValue", .parameters = {}, .returnTypeId = valueId, .function = [](VirtualMachine*) {}
        });
        const auto setter = runtime.addMethod(ownerId, {
            .name = "setValue", .parameters = {{valueId, "value", ""}},
            .returnTypeId = runtime.getVoidType()->getId(), .function = [](VirtualMachine*) {}
        });
        BOOST_REQUIRE(getter.isValid() && setter.isValid());
        BOOST_REQUIRE(runtime.addProperty(ownerId, {
            .valueType = type, .name = "value", .getterMethodId = getter, .setterMethodId = setter
        }).isValid());
        fixture.addFunction("main", NomadString{"params owner:PendingAccessorOwner argument:PendingAccessorValue\n"} +
            source + "\nreturn 0");
        BOOST_TEST(!fixture.compile());
        BOOST_TEST(fixture.getDiagnostics().find("Method-backed native properties are not supported by compiled source yet") !=
            NomadString::npos);
        BOOST_TEST(fixture.getDiagnostics().find("Internal error") == NomadString::npos);
        BOOST_TEST(owner->getFieldStorageSize() == 0U);
    }
}

BOOST_AUTO_TEST_SUITE_END()
