// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/interpreter/Interpreter.hpp>
#include <nomad/interpreter/InterpreterObject.hpp>
#include <nomad/interpreter/InterpreterStruct.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <memory>
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

BOOST_AUTO_TEST_SUITE(interpreter_composites)

BOOST_AUTO_TEST_CASE(struct_wrappers_distinguish_borrowed_storage_owned_copies_and_empty_values) {
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
            InterpreterStruct borrowed(interpreter, *root, storage.data());
            const auto original = borrowed.getValue();
            InterpreterStruct copied(interpreter, original);
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
            InterpreterStruct borrowedEmpty(interpreter, *empty, nullptr);
            const auto emptyValue = borrowedEmpty.getValue();
            InterpreterStruct copiedEmpty(interpreter, emptyValue);
            BOOST_TEST(emptyValue.getType() == empty);
            BOOST_TEST(copiedEmpty.getValue().getStorage().empty());
            borrowed.setProperty(emptyProperty, emptyValue);
            BOOST_TEST(borrowed.getProperty(emptyProperty).getType() == empty);
            BOOST_TEST(borrowed.getProperty(leafProperty).getStorage()[0].getIntegerValue() == 29);
            BOOST_CHECK_THROW(static_cast<void>(borrowed.getProperty("leaf.")), std::invalid_argument);
            BOOST_CHECK_THROW(static_cast<void>(borrowed.getProperty("missing")), std::invalid_argument);
            BOOST_CHECK_THROW((InterpreterStruct{interpreter, *root, nullptr}), std::invalid_argument);
            BOOST_CHECK_THROW((InterpreterStruct{interpreter, number}), std::invalid_argument);
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
        InterpreterObject wrapper(interpreter, *owner, object);
        BOOST_TEST(object->getReferenceCount() == 1U);
        const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{41}});
        wrapper.setProperty(numberProperty, number);
        BOOST_TEST(wrapper.getProperty(numberProperty).getValue().getIntegerValue() == 41);
        {
            const auto snapshot = wrapper.getValue();
            BOOST_TEST(snapshot.getValue().getObjectValue() == object);
            BOOST_TEST(object->getReferenceCount() == 2U);
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

BOOST_AUTO_TEST_CASE(struct_property_copy_failures_preserve_owned_and_borrowed_receivers) {
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
            const auto create = [&]() {
                return owned ? std::make_unique<InterpreterStruct>(interpreter, InterpreterValue(pair, storage)) :
                    std::make_unique<InterpreterStruct>(interpreter, *pair, storage.data());
            };
            field->failCopyAt = 0;
            auto successful = create();
            field->copyCount = 0;
            successful->setProperty(firstProperty, replacement);
            const auto copyStages = field->copyCount;
            successful.reset();
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
                        BOOST_TEST(dynamic_cast<const InterpreterStruct*>(receiver) != nullptr);
                        BOOST_TEST(receiver->getValue().getStorage().empty());
                        BOOST_REQUIRE(interpreter.execute("this", context));
                        BOOST_TEST(context.getResult()->getType() == type);
                    } else {
                        const auto before = receiver->getValue();
                        const auto numberBefore = receiver->getProperty("number").getValue().getIntegerValue();
                        if (type->isStruct()) {
                            BOOST_TEST(dynamic_cast<const InterpreterStruct*>(receiver) != nullptr);
                        } else {
                            BOOST_TEST(dynamic_cast<const InterpreterObject*>(receiver) != nullptr);
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
                auto invalid = current;
                ++invalid.parameterIndex;
                active->restoreExecutionState(invalid);
                BOOST_CHECK_THROW(static_cast<void>(context.getReceiver()), NomadBug);
                BOOST_CHECK_THROW(static_cast<void>(std::as_const(context).getReceiver()), NomadBug);
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

BOOST_AUTO_TEST_SUITE_END()
