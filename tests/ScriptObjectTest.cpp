// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/InstructionDump.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <span>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

using namespace nomad;

static_assert(!std::is_copy_constructible_v<ScriptObject>);
static_assert(!std::is_copy_assignable_v<ScriptObject>);

namespace {

const ClassType* createTestType(Runtime& runtime, const NomadString& name) {
    const auto id = runtime.registerClass(name, ClassMode::Opened);
    BOOST_REQUIRE(id.isValid());
    return runtime.getType(id).value()->asClassType();
}

class CountingObject final : public NomadObject {
public:
    explicit CountingObject(NomadIndex& destroyed): m_destroyed(destroyed) {}
    ~CountingObject() override { ++m_destroyed; }

private:
    NomadIndex& m_destroyed;
};

class InitializingType final : public ClassType {
public:
    InitializingType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        NomadIndex& initialized,
        NomadIndex& destroyed,
        bool fail
    ):
        ClassType(runtime, id, std::move(name), mode),
        m_initialized(initialized),
        m_destroyed(destroyed),
        m_fail(fail)
    {}

    void initValue(RuntimeValue* value) const override {
        ++m_initialized;
        value->setObjectValue(new CountingObject(m_destroyed));
        if (m_fail) {
            throw NomadException("Field initialization failed");
        }
    }

private:
    NomadIndex& m_initialized;
    NomadIndex& m_destroyed;
    bool m_fail;
};

auto constructScriptObject(VirtualMachine& machine, const ClassType* type) {
    BOOST_REQUIRE(type->getConstructors().size() == 1u);
    const auto release = [](ScriptObject* object) { object->releaseRef(); };
    return std::unique_ptr<ScriptObject, decltype(release)>(
        static_cast<ScriptObject*>(machine.createScriptObject(type->getConstructors()[0]->getId())),
        release
    );
}

void compileTestConstructor(
    Runtime& runtime,
    Compiler& compiler,
    const ClassType* type,
    CallableId initializerId
) {
    const auto id = runtime.registerScriptConstructor(type->getId(), {}, "", "");
    BOOST_REQUIRE(id.isValid());
    auto* constructor = runtime.getScriptCallable(id);
    constructor->setFunctionStart(runtime.getInstructions().size());
    compiler.addFunctionEntry(constructor);
    compiler.addOpCode(OpCodes::op_push_this);
    compiler.addOpCode(OpCodes::op_call_script_method);
    compiler.addIndex(runtime.getScriptCallable(initializerId)->getFunctionStart());
    compiler.addOpCode(OpCodes::op_pop_this);
    compiler.addFunctionReturn(constructor);
    constructor->setFunctionEnd(runtime.getInstructions().size());
}

} // namespace

BOOST_AUTO_TEST_SUITE(language_script_object)

BOOST_AUTO_TEST_CASE(fields_use_owner_local_slots_and_zeroed_raw_storage) {
    Runtime runtime;
    const auto* unrelated = createTestType(runtime, "Unrelated");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptClassProperty(unrelated->getId(), "other", runtime.getIntegerType()).isValid());
    const auto* type = createTestType(runtime, "Fields");
    const auto nativeId = runtime.getTypeManager().registerNativeClassProperty(type->getId(), {
        .valueType = runtime.getIntegerType(),
        .name = "native",
        .getter = [](VirtualMachine*, RuntimeValue& result) { result.setIntegerValue(42); }
    });
    BOOST_REQUIRE(nativeId.isValid());
    const auto integerId = runtime.getTypeManager().registerScriptClassProperty(type->getId(), "integer", runtime.getIntegerType());
    const auto floatId = runtime.getTypeManager().registerScriptClassProperty(type->getId(), "number", runtime.getFloatType());
    const auto booleanId = runtime.getTypeManager().registerScriptClassProperty(type->getId(), "flag", runtime.getBooleanType());
    const auto stringId = runtime.getTypeManager().registerScriptClassProperty(type->getId(), "text", runtime.getStringType());
    const auto referenceId = runtime.getTypeManager().registerScriptClassProperty(type->getId(), "reference", type);
    for (const auto id : {integerId, floatId, booleanId, stringId, referenceId}) {
        BOOST_REQUIRE(id.isValid());
    }
    ScriptObject object(type);
    BOOST_TEST(object.getClassType() == type);
    BOOST_TEST(object.getFieldCount() == 5u);
    BOOST_TEST(type->getFieldIndex(nativeId) == NOMAD_INVALID_INDEX);
    BOOST_TEST(type->getFieldIndex(integerId) == 0u);
    BOOST_TEST(type->getFieldIndex(referenceId) == 4u);
    BOOST_TEST(object.getField(type->getFieldIndex(integerId)).getIntegerValue() == 0);
    BOOST_TEST(object.getField(type->getFieldIndex(floatId)).getFloatValue() == 0.0);
    BOOST_TEST(!object.getField(type->getFieldIndex(booleanId)).getBooleanValue());
    BOOST_TEST(object.getField(type->getFieldIndex(stringId)).getObjectValue() == nullptr);
    BOOST_TEST(object.getField(type->getFieldIndex(referenceId)).getObjectValue() == nullptr);
}

BOOST_AUTO_TEST_CASE(inline_struct_fields_use_physical_storage_and_copy_owned_values) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto emptyId = manager.registerStruct("EmptyMarker");
    const auto payloadId = manager.registerStruct("Payload");
    const auto wrapperId = manager.registerStruct("Wrapper");
    const auto ownerId = manager.registerClass("AggregateOwner", ClassMode::Opened);
    const auto* empty = manager.getTypeByName("EmptyMarker")->asStructType();
    const auto* payload = manager.getTypeByName("Payload")->asStructType();
    const auto* wrapper = manager.getTypeByName("Wrapper")->asStructType();
    const auto* owner = manager.getTypeByName("AggregateOwner")->asClassType();
    BOOST_REQUIRE(manager.registerScriptStructProperty(payloadId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(manager.registerScriptStructProperty(payloadId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(manager.registerScriptStructProperty(payloadId, "reference", owner).isValid());
    BOOST_REQUIRE(manager.registerScriptStructProperty(wrapperId, "payload", payload).isValid());
    BOOST_REQUIRE(manager.registerScriptStructProperty(wrapperId, "marker", empty).isValid());
    manager.finalizeCompositeFieldLayout(wrapperId);
    const auto before = manager.registerScriptClassProperty(ownerId, "before", runtime.getIntegerType());
    const auto marker = manager.registerScriptClassProperty(ownerId, "marker", empty);
    const auto first = manager.registerScriptClassProperty(ownerId, "first", wrapper);
    const auto second = manager.registerScriptClassProperty(ownerId, "second", wrapper);
    const auto after = manager.registerScriptClassProperty(ownerId, "after", runtime.getIntegerType());
    for (const auto id : {before, marker, first, second, after}) {
        BOOST_REQUIRE(id.isValid());
    }
    BOOST_TEST(owner->getFieldOffset(first) == 1U);
    BOOST_TEST(owner->getFieldOffset(second) == 4U);
    BOOST_TEST(owner->getFieldOffset(after) == 7U);
    BOOST_TEST(owner->getFieldStorageSize() == 8U);
    const auto firstIndex = owner->getFieldIndex(first);
    const auto secondIndex = owner->getFieldIndex(second);
    const auto afterIndex = owner->getFieldIndex(after);
    auto destroyed = NomadIndex{0};
    {
        ScriptObject object(owner);
        ScriptObject independent(owner);
        const auto& constObject = object;
        BOOST_TEST(object.getFieldCount() == 5U);
        BOOST_TEST(object.getFieldStorage(firstIndex).size() == 3U);
        BOOST_TEST(constObject.getFieldStorage(firstIndex).size() == 3U);
        BOOST_TEST(object.getFieldStorage(owner->getFieldIndex(marker)).empty());
        BOOST_TEST(object.getFieldStorage(firstIndex)[1].getObjectValue() == nullptr);
        object.setFieldStorage(owner->getFieldIndex(marker), {});
        BOOST_CHECK_THROW((void)object.getField(firstIndex), NomadException);
        BOOST_CHECK_THROW(object.setField(firstIndex, RuntimeValue{}), NomadException);
        BOOST_CHECK_THROW((void)object.getField(owner->getFieldIndex(marker)), NomadException);
        BOOST_CHECK_THROW((void)object.getFieldStorage(NOMAD_INVALID_INDEX), NomadException);
        BOOST_CHECK_THROW((void)constObject.getFieldStorage(NOMAD_INVALID_INDEX), NomadException);
        std::array<RuntimeValue, 3> source{};
        std::array<RuntimeValue, 3> scratch{};
        source[0].setIntegerValue(17);
        source[1].setStringValue("copied");
        source[2].setObjectValue(new CountingObject(destroyed));
        BOOST_CHECK_THROW(object.setFieldStorage(firstIndex, source), NomadException);
        BOOST_CHECK_THROW(object.setFieldStorage(firstIndex, std::span(source).first(2), scratch), NomadException);
        BOOST_CHECK_THROW(object.setFieldStorage(firstIndex, source, std::span(scratch).first(2)), NomadException);
        BOOST_TEST(source[2].getObjectValue()->getReferenceCount() == 1U);
        object.setFieldStorage(firstIndex, source, scratch);
        object.setFieldStorage(secondIndex, constObject.getFieldStorage(firstIndex), scratch);
        object.setFieldStorage(firstIndex, constObject.getFieldStorage(firstIndex), scratch);
        BOOST_TEST(source[1].getObjectValue()->getReferenceCount() == 3U);
        BOOST_TEST(source[2].getObjectValue()->getReferenceCount() == 3U);
        object.getFieldStorage(firstIndex)[0].setIntegerValue(23);
        BOOST_TEST(source[0].getIntegerValue() == 17);
        BOOST_TEST(object.getFieldStorage(secondIndex)[0].getIntegerValue() == 17);
        BOOST_TEST(independent.getFieldStorage(firstIndex)[0].getIntegerValue() == 0);
        object.setField(afterIndex, RuntimeValue(NomadInteger{42}));
        BOOST_TEST(object.getField(afterIndex).getIntegerValue() == 42);
        BOOST_TEST(object.getField(owner->getFieldIndex(before)).getIntegerValue() == 0);
        const auto* storage = object.getFieldStorage(firstIndex).data();
        BOOST_REQUIRE(manager.registerScriptClassProperty(ownerId, "late", runtime.getFloatType()).isValid());
        BOOST_TEST(object.getFieldStorage(firstIndex).data() == storage);
        BOOST_TEST(object.getFieldCount() == 5U);
        BOOST_CHECK_THROW((void)object.getFieldStorage(5), NomadException);
        wrapper->freeValue(source.data());
        BOOST_TEST(destroyed == 0U);
    }
    BOOST_TEST(destroyed == 1U);
}

BOOST_AUTO_TEST_CASE(zero_and_single_slot_structs_do_not_use_scalar_field_access) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto emptyId = manager.registerStruct("Zero");
    const auto scalarId = manager.registerStruct("One");
    const auto* empty = manager.getTypeByName("Zero")->asStructType();
    const auto* scalar = manager.getTypeByName("One")->asStructType();
    BOOST_REQUIRE(manager.registerScriptStructProperty(scalarId, "value", runtime.getIntegerType()).isValid());
    const auto ownerId = manager.registerClass("ZeroOwner", ClassMode::Closed);
    const auto* owner = manager.getTypeByName("ZeroOwner")->asClassType();
    BOOST_REQUIRE(manager.registerScriptClassProperty(ownerId, "first", empty).isValid());
    BOOST_REQUIRE(manager.registerScriptClassProperty(ownerId, "second", empty).isValid());
    BOOST_CHECK_THROW(ScriptObject{owner}, NomadException);
    manager.finalizeCompositeFieldLayout(ownerId);
    ScriptObject object(owner);
    BOOST_TEST(object.getFieldCount() == 2U);
    BOOST_TEST(owner->getFieldStorageSize() == 0U);
    BOOST_TEST(object.getFieldStorage(0).empty());
    BOOST_TEST(object.getFieldStorage(1).empty());
    object.setFieldStorage(0, {});
    object.setFieldStorage(1, {});
    BOOST_CHECK_THROW((void)object.getField(0), NomadException);
    BOOST_CHECK_THROW(object.setFieldStorage(0, std::array<RuntimeValue, 1>{}), NomadException);
    const auto scalarOwnerId = manager.registerClass("OneOwner", ClassMode::Closed);
    const auto* scalarOwner = manager.getTypeByName("OneOwner")->asClassType();
    BOOST_REQUIRE(manager.registerScriptClassProperty(scalarOwnerId, "value", scalar).isValid());
    manager.finalizeCompositeFieldLayout(scalarOwnerId);
    ScriptObject single(scalarOwner);
    BOOST_CHECK_THROW(single.setField(0, RuntimeValue(NomadInteger{7})), NomadException);
    const std::array<RuntimeValue, 1> value{RuntimeValue(NomadInteger{7})};
    std::array<RuntimeValue, 1> unusedScratch{RuntimeValue(NomadInteger{99})};
    single.setFieldStorage(0, value, std::span(unusedScratch).first(0));
    BOOST_TEST(single.getFieldStorage(0)[0].getIntegerValue() == 7);
    BOOST_TEST(unusedScratch[0].getIntegerValue() == 99);
}

BOOST_AUTO_TEST_CASE(aggregate_field_assignment_supports_overlapping_source_storage) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto tripleId = manager.registerStruct("Triple");
    const auto* triple = manager.getTypeByName("Triple")->asStructType();
    for (const auto* name : {"first", "second", "third"}) {
        BOOST_REQUIRE(manager.registerScriptStructProperty(tripleId, name, runtime.getIntegerType()).isValid());
    }
    const auto ownerId = manager.registerClass("Overlapping", ClassMode::Closed);
    const auto* owner = manager.getTypeByName("Overlapping")->asClassType();
    BOOST_REQUIRE(manager.registerScriptClassProperty(ownerId, "first", triple).isValid());
    BOOST_REQUIRE(manager.registerScriptClassProperty(ownerId, "second", triple).isValid());
    manager.finalizeCompositeFieldLayout(ownerId);
    ScriptObject object(owner);
    const std::array<RuntimeValue, 3> first{
        RuntimeValue(NomadInteger{1}), RuntimeValue(NomadInteger{2}), RuntimeValue(NomadInteger{3})
    };
    const std::array<RuntimeValue, 3> second{
        RuntimeValue(NomadInteger{4}), RuntimeValue(NomadInteger{5}), RuntimeValue(NomadInteger{6})
    };
    std::array<RuntimeValue, 3> scratch{};
    object.setFieldStorage(0, first, scratch);
    object.setFieldStorage(1, second, scratch);
    const auto storage = object.getFieldStorage(0);
    BOOST_TEST(storage.data() + storage.size() == object.getFieldStorage(1).data());
    const std::span<const RuntimeValue> overlapping(storage.data() + 1, 3);
    object.setFieldStorage(0, overlapping, scratch);
    BOOST_TEST(object.getFieldStorage(0)[0].getIntegerValue() == 2);
    BOOST_TEST(object.getFieldStorage(0)[1].getIntegerValue() == 3);
    BOOST_TEST(object.getFieldStorage(0)[2].getIntegerValue() == 4);
    BOOST_TEST(object.getFieldStorage(1)[0].getIntegerValue() == 4);
}

BOOST_AUTO_TEST_CASE(allocation_and_stores_do_not_run_type_initializers) {
    Runtime runtime;
    auto initialized = NomadIndex{0};
    auto destroyed = NomadIndex{0};
    const auto valueId = runtime.registerClass<InitializingType>(
        "Counted", ClassMode::Closed, initialized, destroyed, false
    );
    BOOST_REQUIRE(valueId.isValid());
    const auto* type = createTestType(runtime, "Owner");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptClassProperty(type->getId(), "value", runtime.getType(valueId).value()).isValid());
    {
        ScriptObject uninitialized(type);
        BOOST_TEST(initialized == 0u);
        BOOST_TEST(uninitialized.getField(0).getObjectValue() == nullptr);
        uninitialized.setField(0, RuntimeValue{});
    }
    BOOST_TEST(destroyed == 0u);
    {
        ScriptObject first(type);
        ScriptObject second(type);
        RuntimeValue value;
        value.setObjectValue(new CountingObject(destroyed));
        first.setField(0, value);
        value.freeObjectValue();
        value.setObjectValue(new CountingObject(destroyed));
        second.setField(0, value);
        value.freeObjectValue();
        BOOST_TEST(initialized == 0u);
        BOOST_TEST(first.getField(0).getObjectValue() != second.getField(0).getObjectValue());
    }
    BOOST_TEST(destroyed == 2u);
}

BOOST_AUTO_TEST_CASE(instances_have_independent_values_and_aliases_share_the_same_instance) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Point");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptClassProperty(type->getId(), "value", runtime.getIntegerType()).isValid());
    RuntimeValue owner;
    owner.setObjectValue(new ScriptObject(type));
    auto* first = owner.asObject<ScriptObject>();
    ScriptObject second(type);
    RuntimeValue alias;
    type->copyValue(&owner, &alias);
    BOOST_TEST(first->getReferenceCount() == 2u);
    alias.asObject<ScriptObject>()->setField(0, RuntimeValue(NomadInteger{17}));
    BOOST_TEST(first->getField(0).getIntegerValue() == 17);
    BOOST_TEST(second.getField(0).getIntegerValue() == 0);
    type->freeValue(&owner);
    BOOST_TEST(alias.asObject<ScriptObject>()->getField(0).getIntegerValue() == 17);
    type->freeValue(&alias);
}

BOOST_AUTO_TEST_CASE(field_replacement_retains_references_and_handles_self_assignment) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Owner");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptClassProperty(type->getId(), "first", type).isValid());
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptClassProperty(type->getId(), "second", type).isValid());
    auto destroyed = NomadIndex{0};
    {
        ScriptObject object(type);
        RuntimeValue value;
        value.setObjectValue(new CountingObject(destroyed));
        object.setField(0, value);
        object.setField(1, object.getField(0));
        object.setField(0, object.getField(0));
        BOOST_TEST(value.getObjectValue()->getReferenceCount() == 3u);
        type->freeValue(&value);
        object.setField(0, RuntimeValue{});
        BOOST_TEST(destroyed == 0u);
        BOOST_TEST(object.getField(1).getObjectValue()->getReferenceCount() == 1u);
        object.setField(1, RuntimeValue{});
        BOOST_TEST(destroyed == 1u);
        RuntimeValue remaining;
        remaining.setObjectValue(new CountingObject(destroyed));
        object.setField(0, remaining);
        type->freeValue(&remaining);
    }
    BOOST_TEST(destroyed == 2u);
}

BOOST_AUTO_TEST_CASE(partial_out_of_order_stores_release_values_without_initialization_tracking) {
    Runtime runtime;
    auto initialized = NomadIndex{0};
    auto destroyed = NomadIndex{0};
    const auto countedId = runtime.registerClass<InitializingType>(
        "Counted", ClassMode::Closed, initialized, destroyed, false
    );
    BOOST_REQUIRE(countedId.isValid());
    const auto* type = createTestType(runtime, "Partial");
    for (auto index = NomadIndex{0}; index < 3; ++index) {
        BOOST_REQUIRE(runtime.getTypeManager().registerScriptClassProperty(type->getId(), "field" + std::to_string(index),
            runtime.getType(countedId).value()).isValid());
    }
    {
        ScriptObject object(type);
        for (const auto index : {NomadIndex{2}, NomadIndex{0}}) {
            RuntimeValue value;
            value.setObjectValue(new CountingObject(destroyed));
            object.setField(index, value);
            value.freeObjectValue();
        }
        BOOST_TEST(object.getField(1).getObjectValue() == nullptr);
        object.setField(2, RuntimeValue{});
        BOOST_TEST(destroyed == 1u);
        BOOST_TEST(initialized == 0u);
    }
    BOOST_TEST(destroyed == 2u);
}

BOOST_AUTO_TEST_CASE(layout_does_not_resize_and_invalid_access_is_rejected) {
    Runtime runtime;
    BOOST_CHECK_THROW(ScriptObject(nullptr), NomadException);
    const auto* type = createTestType(runtime, "Layout");
    const auto firstId = runtime.getTypeManager().registerScriptClassProperty(type->getId(), "first", runtime.getIntegerType());
    BOOST_REQUIRE(firstId.isValid());
    ScriptObject object(type);
    const auto laterId = runtime.getTypeManager().registerScriptClassProperty(type->getId(), "later", runtime.getFloatType());
    BOOST_REQUIRE(laterId.isValid());
    BOOST_TEST(object.getFieldCount() == 1u);
    BOOST_TEST(object.getField(0).getIntegerValue() == 0);
    BOOST_CHECK_THROW((void)object.getField(type->getFieldIndex(laterId)), NomadException);
    BOOST_CHECK_THROW(object.setField(1, RuntimeValue{}), NomadException);
    BOOST_CHECK_THROW((void)object.getField(NOMAD_INVALID_INDEX), NomadException);
    const auto* emptyType = createTestType(runtime, "Empty");
    ScriptObject empty(emptyType);
    BOOST_TEST(empty.getFieldCount() == 0u);
    BOOST_CHECK_THROW((void)empty.getField(0), NomadException);
}

BOOST_AUTO_TEST_CASE(compiled_and_interpreted_properties_share_storage_and_preserve_ownership) {
    for (const auto interpreted : {false, true}) {
        Runtime runtime;
        const auto* type = createTestType(runtime, "Stored");
        BOOST_REQUIRE(runtime.getTypeManager().registerScriptClassProperty(type->getId(), "text", runtime.getStringType()).isValid());
        BOOST_REQUIRE(runtime.getTypeManager().registerScriptClassProperty(type->getId(), "next", type).isValid());
        BOOST_REQUIRE(runtime.getTypeManager().registerNativeClassProperty(type->getId(), {
            .valueType = runtime.getIntegerType(),
            .name = "nativeLength",
            .getter = [](VirtualMachine* machine, RuntimeValue& result) {
                const auto* object = machine->getThisAs<ScriptObject>();
                result.setIntegerValue(static_cast<NomadInteger>(
                    NomadString(object->getField(0).getStringValue()).size()
                ));
            },
            .setter = {},
            .documentation = ""
        }).isValid());
        BOOST_REQUIRE(runtime.addConstructor(type->getId(), {
            .parameters = {},
            .function = [type](VirtualMachine* machine) {
                auto object = std::make_unique<ScriptObject>(type);
                machine->getResult().setObjectValue(object.release());
            },
            .documentation = ""
        }).isValid());
        const auto before = NomadObject::getLiveObjectCount();
        {
            const NomadString source =
                "first = new Stored\n"
                "second = new Stored\n"
                "alias = first\n"
                "alias.text = \"hello\"\n"
                "first.next = second\n"
                "second.text = first.text\n"
                "first.text = first.text\n"
                "first.text = \"replaced\"\n";
            if (interpreted) {
                auto machine = runtime.createVirtualMachine();
                const Interpreter contextEvaluator(*machine);
                InterpreterContext context(contextEvaluator);
                for (auto start = NomadString::size_type{0}; start < source.size();) {
                    const auto end = source.find('\n', start);
                    const auto line = source.substr(start, end - start);
                    const auto executed = context.getInterpreter().execute(line, context);
                    BOOST_REQUIRE_MESSAGE(executed,
                        line + ": " + (context.getError() != nullptr ? *context.getError() : "No error reported"));
                    start = end + 1;
                }
                BOOST_REQUIRE(context.getInterpreter().execute("first.next.text", context));
                BOOST_REQUIRE(context.getResult() != nullptr);
                BOOST_TEST(NomadString(context.getResult()->getValue().getStringValue()) == "hello");
                BOOST_REQUIRE(context.getInterpreter().execute("second.nativeLength", context));
                BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 5);
                BOOST_TEST(!context.getInterpreter().execute("second.nativeLength = 2", context));
                BOOST_TEST(!context.getInterpreter().execute("second.missing = 2", context));
                BOOST_TEST(type->getProperty("missing") == nullptr);
                BOOST_TEST(!context.getInterpreter().execute("second.text = 2", context));
                BOOST_TEST(!context.getInterpreter().execute("second.next.text", context));
                BOOST_REQUIRE(context.getError() != nullptr);
                BOOST_REQUIRE(context.getInterpreter().execute("alias.text", context));
                BOOST_TEST(NomadString(context.getResult()->getValue().getStringValue()) == "replaced");
            } else {
                auto compiler = runtime.createCompiler();
                CompilerContext context(compiler.get());
                compiler->registerSources(&context, {{{"test", {"test.nomad",
                    source + "if second.nativeLength != 5\nreturn \"invalid length\"\nend\n"
                        "return first.next.text\n"}}}, {}});
                BOOST_REQUIRE(compiler->compileFunctions(&context));
                const auto beforeExecution = NomadObject::getLiveObjectCount();
                RuntimeValue result;
                runtime.executeFunction(runtime.getFunctionId("test"), {}, result);
                BOOST_TEST(NomadString(result.getStringValue()) == "hello");
                runtime.getStringType()->freeValue(&result);
                BOOST_TEST(NomadObject::getLiveObjectCount() == beforeExecution);
            }
        }
        if (interpreted) {
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        }
    }
}

BOOST_AUTO_TEST_CASE(property_dispatch_rejects_missing_storage_wrong_owners_and_invalid_ids) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Owner");
    const auto* other = createTestType(runtime, "Other");
    const auto id = runtime.getTypeManager().registerScriptClassProperty(type->getId(), "value", runtime.getStringType());
    BOOST_REQUIRE(id.isValid());
    auto machine = runtime.createVirtualMachine();
    ScriptObject object(type);
    ScriptObject wrong(other);
    RuntimeValue receiver;
    RuntimeValue result;
    BOOST_CHECK_THROW(machine->getProperty(id, receiver, result), NomadException);
    BOOST_CHECK_THROW(machine->setProperty(id, receiver, result), NomadException);
    receiver.setObjectValue(&wrong);
    BOOST_CHECK_THROW(machine->getProperty(id, receiver, result), NomadException);
    BOOST_CHECK_THROW(machine->setProperty(id, receiver, result), NomadException);
    auto destroyed = NomadIndex{0};
    CountingObject native(destroyed);
    receiver.setObjectValue(&native);
    BOOST_CHECK_THROW(machine->getProperty(id, receiver, result), NomadException);
    BOOST_CHECK_THROW(machine->setProperty(id, receiver, result), NomadException);
    receiver.setObjectValue(&object);
    machine->getProperty(id, receiver, result);
    BOOST_TEST(result.getObjectValue() == nullptr);
    machine->setProperty(id, receiver, result);
    BOOST_CHECK_THROW(machine->getProperty(PropertyId::INVALID, receiver, result), NomadException);
    BOOST_CHECK_THROW(machine->setProperty(PropertyId::INVALID, receiver, result), NomadException);
    machine->getProperty(id, receiver, result);
    BOOST_TEST(result.getObjectValue() == nullptr);
    runtime.getStringType()->freeValue(&result);
}

BOOST_AUTO_TEST_CASE(generated_initializer_runs_explicit_expressions_in_source_order_then_late_defaults) {
    test::LanguageTestFixture fixture;
    auto calls = 0;
    BOOST_REQUIRE(fixture.getRuntime().registerNativeFunction(
        "nextValue", [&](VirtualMachine* machine) { machine->setIntegerResult(++calls); },
        {}, fixture.getRuntime().getIntegerType(), ""
    ).isValid());
    fixture.addFunction("main",
        "open class Point\n"
        "first = nextValue\n"
        "second = 42\n"
        "third = nextValue\n"
        "description = \"point\"\n"
        "end\n"
        "fun extend point:Point enabled:bool\n"
        "if enabled\npoint.conditional = 7\nend\nend"
    );
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto* type = runtime.getTypeByName("Point")->asClassType();
    for (const auto* name : {"first", "second", "third", "description"}) {
        BOOST_TEST(type->getProperty(name)->hasExplicitInitializer());
    }
    BOOST_TEST(!type->getProperty("conditional")->hasExplicitInitializer());
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        const auto object = constructScriptObject(*machine, type);
        BOOST_TEST(object->getField(type->getFieldIndex(type->getProperty("first")->getId())).getIntegerValue() == 1);
        BOOST_TEST(object->getField(type->getFieldIndex(type->getProperty("second")->getId())).getIntegerValue() == 42);
        BOOST_TEST(object->getField(type->getFieldIndex(type->getProperty("third")->getId())).getIntegerValue() == 2);
        BOOST_TEST(object->getField(type->getFieldIndex(type->getProperty("conditional")->getId())).getIntegerValue() == 0);
        BOOST_TEST(NomadString(object->getField(
            type->getFieldIndex(type->getProperty("description")->getId())).getStringValue()) == "point");
        BOOST_TEST(calls == 2);
        BOOST_TEST(object->getReferenceCount() == 1u);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(generated_initializers_run_per_instance_and_preserve_fresh_object_values) {
    test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto* valueType = createTestType(runtime, "Value");
    auto destroyed = NomadIndex{0};
    auto created = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "fresh", [&](VirtualMachine* machine) {
            ++created;
            machine->getResult().setObjectValue(new CountingObject(destroyed));
        }, {}, valueType, ""
    ).isValid());
    fixture.addFunction("main", "class Owner\nvalue = fresh\nend");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(created == 0u);
    const auto* type = runtime.getTypeByName("Owner")->asClassType();
    {
        auto machine = runtime.createVirtualMachine();
        const auto first = constructScriptObject(*machine, type);
        const auto second = constructScriptObject(*machine, type);
        BOOST_TEST(created == 2u);
        BOOST_TEST(first->getField(0).getObjectValue() != second->getField(0).getObjectValue());
        BOOST_TEST(first->getField(0).getObjectValue()->getReferenceCount() == 1u);
        BOOST_TEST(second->getField(0).getObjectValue()->getReferenceCount() == 1u);
    }
    BOOST_TEST(destroyed == 2u);
}

BOOST_AUTO_TEST_CASE(initializer_source_order_is_independent_of_property_registration_order) {
    test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto calls = NomadInteger{0};
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "nextValue", [&](VirtualMachine* machine) { machine->setIntegerResult(++calls); },
        {}, runtime.getIntegerType(), ""
    ).isValid());
    fixture.addFunction("main",
        "class Fields\nfirst = forwardValue\nsecond = nextValue\n"
        "init enabled:bool\nthis.first = 7\nif enabled\nthis.late = \"late\"\nend\nend\nend\n"
        "fun forwardValue\nreturn nextValue\nend\nreturn new Fields false"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* type = runtime.getTypeByName("Fields")->asClassType();
    BOOST_REQUIRE(type->getProperties().size() == 3u);
    BOOST_TEST(type->getProperties()[0]->getName() == "second");
    BOOST_TEST(type->getProperty("first")->hasExplicitInitializer());
    BOOST_TEST(type->getProperty("second")->hasExplicitInitializer());
    BOOST_TEST(!type->getProperty("late")->hasExplicitInitializer());
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    const auto* object = result.asObject<ScriptObject>();
    BOOST_TEST(calls == 2);
    BOOST_TEST(object->getField(type->getFieldIndex(type->getProperty("first")->getId())).getIntegerValue() == 7);
    BOOST_TEST(object->getField(type->getFieldIndex(type->getProperty("second")->getId())).getIntegerValue() == 2);
    const auto& late = object->getField(type->getFieldIndex(type->getProperty("late")->getId()));
    BOOST_REQUIRE(late.getObjectValue() != nullptr);
    BOOST_TEST(NomadString(late.getStringValue()).empty());
    result.freeObjectValue();
}

BOOST_AUTO_TEST_CASE(generated_defaults_honor_type_initialization_only_for_fields_without_initializers) {
    test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    auto initialized = NomadIndex{0};
    auto destroyed = NomadIndex{0};
    auto explicitCalls = NomadIndex{0};
    const auto valueId = runtime.registerClass<InitializingType>(
        "Counted", ClassMode::Closed, initialized, destroyed, false
    );
    BOOST_REQUIRE(valueId.isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "fresh", [&](VirtualMachine* machine) {
            BOOST_TEST(initialized == 0u);
            ++explicitCalls;
            machine->getResult().setObjectValue(new CountingObject(destroyed));
        }, {}, runtime.getType(valueId).value(), ""
    ).isValid());
    fixture.addFunction("main",
        "open class Owner\nvalue = fresh\n"
        "init enabled:bool\nif enabled\nthis.late = fresh\nend\nend\nend\n"
        "fun extend owner:Owner enabled:bool\nif enabled\nowner.external = fresh\nend\nend\n"
        "return new Owner false"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(initialized == 0u);
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
    BOOST_TEST(explicitCalls == 1u);
    BOOST_TEST(initialized == 2u);
    auto* object = result.asObject<ScriptObject>();
    BOOST_TEST(object->getFieldCount() == 3u);
    for (auto index = NomadIndex{0}; index < object->getFieldCount(); ++index) {
        BOOST_REQUIRE(object->getField(index).getObjectValue() != nullptr);
        BOOST_TEST(object->getField(index).getObjectValue()->getReferenceCount() == 1u);
    }
    result.freeObjectValue();
    BOOST_TEST(destroyed == 3u);
}

BOOST_AUTO_TEST_CASE(host_construction_releases_stored_fields_on_initializer_failure_and_can_construct_again) {
    test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto* valueType = createTestType(runtime, "Value");
    auto destroyed = NomadIndex{0};
    auto calls = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "fresh", [&](VirtualMachine* machine) {
            machine->getResult().setObjectValue(new CountingObject(destroyed));
        }, {}, valueType, ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "fail", [&](VirtualMachine*) {
            ++calls;
            throw NomadException("Initializer failed");
        }, {}, runtime.getIntegerType(), ""
    ).isValid());
    fixture.addFunction("main",
        "class Broken\nvalue = fresh\nnumber = fail\nunreached = fresh\nend\n"
        "class Working\nvalue = 42\nend"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* broken = runtime.getTypeByName("Broken")->asClassType();
    const auto* working = runtime.getTypeByName("Working")->asClassType();
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        BOOST_CHECK_THROW(constructScriptObject(*machine, broken), NomadException);
        BOOST_TEST(calls == 1u);
        BOOST_CHECK_THROW(constructScriptObject(*machine, broken), NomadException);
        BOOST_TEST(calls == 2u);
        const auto recovered = constructScriptObject(*machine, working);
        BOOST_TEST(recovered->getField(0).getIntegerValue() == 42);
    }
    BOOST_TEST(destroyed == 2u);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(empty_initializers_create_objects) {
    test::LanguageTestFixture fixture;
    fixture.addFunction("main", "class Empty\nend");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    auto machine = runtime.createVirtualMachine();
    const auto* type = runtime.getTypeByName("Empty")->asClassType();
    const auto empty = constructScriptObject(*machine, type);
    BOOST_TEST(empty->getFieldCount() == 0u);
    BOOST_TEST(empty->getReferenceCount() == 1u);
}

BOOST_AUTO_TEST_CASE(script_receiver_frames_restore_across_script_and_native_calls) {
    test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "checkFree", [](VirtualMachine* machine) {
            BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        }, {}, runtime.getVoidType(), ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "failFree", [](VirtualMachine* machine) {
            BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
            throw NomadException("Free call failed");
        }, {}, runtime.getVoidType(), ""
    ).isValid());
    fixture.addFunction("helper", "checkFree\nreturn");
    fixture.addFunction("failingHelper", "failFree\nreturn");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* type = createTestType(runtime, "Receiver");
    BOOST_REQUIRE(runtime.getTypeManager().registerScriptClassProperty(type->getId(), "visits", runtime.getIntegerType()).isValid());
    const auto initializerId = runtime.registerScriptMethod(type->getId(), "Receiver$init", {}, "", "");
    BOOST_REQUIRE(initializerId.isValid());
    auto* initializer = runtime.getScriptCallable(initializerId);
    const auto propertyId = runtime.getTypeManager().registerNativeClassProperty(type->getId(), {
        .valueType = runtime.getIntegerType(),
        .name = "native",
        .getter = [](VirtualMachine* machine, RuntimeValue& result) {
            auto* receiver = machine->getThis();
            machine->run(machine->getRuntime()->getScriptFunction(
                machine->getRuntime()->getFunctionId("helper")), {});
            BOOST_TEST(machine->getThis() == receiver);
            BOOST_CHECK_THROW(machine->run(machine->getRuntime()->getScriptFunction(
                machine->getRuntime()->getFunctionId("failingHelper")), {}), NomadException);
            BOOST_TEST(machine->getThis() == receiver);
            BOOST_CHECK_THROW(
                static_cast<void>(Interpreter{*machine}.runFunction(CallableId::INVALID, {})), std::invalid_argument
            );
            BOOST_TEST(machine->getThis() == receiver);
            result.setIntegerValue(42);
        },
        .setter = {},
        .documentation = ""
    });
    BOOST_REQUIRE(propertyId.isValid());
    const auto probe = runtime.registerInstruction("test.receiver", [](VirtualMachine* machine) {
        auto* object = machine->getThisAs<ScriptObject>();
        BOOST_TEST(object->getClassType()->getTypeName() == "Receiver");
        BOOST_TEST(object->getReferenceCount() == 1u);
        const auto* initializer = object->getClassType()->getMethods()[0]->asScriptCallable();
        BOOST_TEST(initializer->getVariableId("this").isInvalid());
        RuntimeValue receiver;
        receiver.setObjectValue(object);
        RuntimeValue result;
        machine->getProperty(object->getClassType()->getProperty("native")->getId(), receiver, result);
        BOOST_TEST(result.getIntegerValue() == 42);
        BOOST_TEST(machine->getThis() == object);
        object->setField(0, RuntimeValue{object->getField(0).getIntegerValue() + 1});
    }, "", {});
    auto compiler = runtime.createCompiler();
    initializer->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(initializer);
    compiler->addOpCode(OpCodes::op_code_load_default_r);
    compiler->addId(runtime.getIntegerType()->getId().id);
    compiler->addOpCode(OpCodes::op_copy_this_to_i);
    compiler->addOpCode(OpCodes::op_native_property_set);
    compiler->addId(type->getProperty("visits")->getId().id);
    compiler->addOpCode(OpCodes::op_object_free_i);
    compiler->addOpCode(probe);
    compiler->addOpCode(OpCodes::op_push_this);
    compiler->addOpCode(OpCodes::op_call_script_function);
    compiler->addIndex(runtime.getScriptFunction(runtime.getFunctionId("helper"))->getFunctionStart());
    compiler->addOpCode(OpCodes::op_pop_this);
    compiler->addOpCode(probe);
    compiler->addFunctionReturn(initializer);
    initializer->setFunctionEnd(runtime.getInstructions().size());
    compileTestConstructor(runtime, *compiler, type, initializerId);
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        const auto object = constructScriptObject(*machine, type);
        BOOST_TEST(object->getField(0).getIntegerValue() == 2);
        BOOST_TEST(object->getReferenceCount() == 1u);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(script_receiver_restores_after_nested_host_construction_and_failure) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto probe = runtime.registerInstruction("test.nestedReceiver", [](VirtualMachine* machine) {
        auto* object = machine->getThisAs<ScriptObject>();
        BOOST_TEST(object->getReferenceCount() == 1u);
        const auto& name = object->getClassType()->getTypeName();
        if (name == "BrokenReceiver") {
            throw NomadException("Receiver frame failed");
        }
        if (name == "OuterReceiver") {
            for (const auto* nestedName : {"InnerReceiver", "BrokenReceiver"}) {
                const auto* nestedType = machine->getRuntime()->getTypeByName(nestedName)->asClassType();
                if (NomadString(nestedName) == "BrokenReceiver") {
                    BOOST_CHECK_THROW(constructScriptObject(*machine, nestedType), NomadException);
                } else {
                    const auto nested = constructScriptObject(*machine, nestedType);
                    BOOST_TEST(nested->getReferenceCount() == 1u);
                }
                BOOST_TEST(machine->getThis() == object);
            }
        }
    }, "", {});
    for (const auto* name : {"OuterReceiver", "InnerReceiver", "BrokenReceiver"}) {
        const auto* type = createTestType(runtime, name);
        const auto id = runtime.registerScriptMethod(type->getId(), type->getInitializerName(), {}, "", "");
        BOOST_REQUIRE(id.isValid());
        auto* initializer = runtime.getScriptCallable(id);
        initializer->setFunctionStart(runtime.getInstructions().size());
        compiler->addFunctionEntry(initializer);
        compiler->addOpCode(probe);
        compiler->addFunctionReturn(initializer);
        initializer->setFunctionEnd(runtime.getInstructions().size());
        compileTestConstructor(runtime, *compiler, type, id);
    }
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        const auto outer = constructScriptObject(*machine, runtime.getTypeByName("OuterReceiver")->asClassType());
        BOOST_TEST(outer->getReferenceCount() == 1u);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        BOOST_CHECK_THROW(constructScriptObject(*machine,
            runtime.getTypeByName("BrokenReceiver")->asClassType()), NomadException);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        const auto recovered = constructScriptObject(*machine, runtime.getTypeByName("InnerReceiver")->asClassType());
        BOOST_TEST(recovered->getReferenceCount() == 1u);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(compiled_field_access_uses_full_width_physical_offsets_and_native_fallback) {
    Runtime runtime;
    const Type* prefix = runtime.getIntegerType();
    for (auto index = 0; prefix->getSize() <= toNomadIndex(NOMAD_ID_MAX); ++index) {
        const auto name = "FieldPadding" + std::to_string(index);
        const auto id = runtime.registerStruct(name);
        BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(id, "left", prefix).isValid());
        BOOST_REQUIRE(runtime.getTypeManager().registerScriptStructProperty(id, "right", prefix).isValid());
        runtime.getTypeManager().finalizeCompositeFieldLayout(id);
        prefix = runtime.getTypeByName(name);
    }
    const auto ownerId = runtime.registerClass("PhysicalFields", ClassMode::Closed);
    const auto prefixId = runtime.getTypeManager().registerScriptClassProperty(ownerId, "padding", prefix);
    const auto numberId = runtime.getTypeManager().registerScriptClassProperty(ownerId, "number", runtime.getIntegerType());
    const auto textId = runtime.getTypeManager().registerScriptClassProperty(ownerId, "text", runtime.getStringType());
    auto accessorCalls = 0;
    const auto accessorId = runtime.getTypeManager().registerNativeClassProperty(ownerId, {
        .valueType = runtime.getIntegerType(),
        .name = "accessor",
        .getter = [&](VirtualMachine*, RuntimeValue& result) {
            ++accessorCalls;
            result.setIntegerValue(41);
        }
    });
    BOOST_REQUIRE(prefixId.isValid());
    BOOST_REQUIRE(numberId.isValid());
    BOOST_REQUIRE(textId.isValid());
    BOOST_REQUIRE(accessorId.isValid());
    runtime.getTypeManager().finalizeCompositeFieldLayout(ownerId);
    const auto* owner = runtime.getTypeByName("PhysicalFields")->asClassType();
    const auto wide = prefix->getSize();
    BOOST_TEST(wide > NOMAD_ID_MAX);
    const auto functionId = runtime.registerScriptFunction("fieldEmission", "fields.nomad", "");
    auto* function = runtime.getScriptFunction(functionId);
    function->setReturnType(runtime.getVoidType());
    auto compiler = runtime.createCompiler();
    function->setFunctionStart(runtime.getInstructions().size());
    const auto numberLoad = runtime.getInstructions().size();
    compiler->addLoadProperty(numberId);
    const auto numberStore = runtime.getInstructions().size();
    compiler->addStoreProperty(numberId);
    const auto textLoad = runtime.getInstructions().size();
    compiler->addLoadProperty(textId);
    const auto textStore = runtime.getInstructions().size();
    compiler->addStoreProperty(textId);
    const auto nativeLoad = runtime.getInstructions().size();
    compiler->addLoadProperty(accessorId);
    const auto aggregateLoad = runtime.getInstructions().size();
    compiler->addLoadProperty(prefixId);
    function->setFunctionEnd(runtime.getInstructions().size());
    const auto& instructions = runtime.getInstructions();
    BOOST_TEST(instructions[numberLoad + 1].value.getIndexValue() == wide);
    BOOST_TEST(instructions[numberStore + 1].value.getIndexValue() == wide);
    BOOST_TEST(instructions[textLoad + 1].value.getIndexValue() == wide + 1);
    BOOST_TEST(instructions[textStore + 1].value.getIndexValue() == wide + 1);
    BOOST_TEST(instructions[nativeLoad + 1].value.getIdValue() == accessorId.id);
    BOOST_TEST(runtime.getInstructionId(instructions[aggregateLoad].fn).id ==
        runtime.getInstructionId(OpCodes::op_native_property_get).id);
    std::ostringstream dump;
    dumpInstructions(runtime, *function, dump);
    BOOST_TEST(dump.str().find(std::to_string(wide + 1)) != NomadString::npos);
    BOOST_TEST(dump.str().find(OpCodes::op_object_stored_property_set) != NomadString::npos);
    ScriptObject object(owner);
    VirtualMachine machine(&runtime);
    RuntimeValue receiver;
    receiver.setObjectValue(&object);
    machine.setIntermediate(receiver);
    const auto execute = [&](const NomadIndex address) {
        machine.jump(address + 1);
        instructions[address].fn(&machine);
    };
    machine.setIntegerResult(17);
    execute(numberStore);
    execute(numberLoad);
    BOOST_TEST(machine.getIntegerResult() == 17);
    BOOST_TEST(object.getField(owner->getFieldIndex(numberId)).getIntegerValue() == 17);
    machine.setStringResult("retained");
    execute(textStore);
    BOOST_TEST(machine.getStringResult()->getReferenceCount() == 2U);
    execute(textStore);
    BOOST_TEST(machine.getStringResult()->getReferenceCount() == 2U);
    machine.freeObjectResult();
    execute(textLoad);
    BOOST_TEST(NomadString(machine.getResult().getStringValue()) == "retained");
    BOOST_TEST(machine.getStringResult()->getReferenceCount() == 2U);
    machine.freeObjectResult();
    execute(nativeLoad);
    BOOST_TEST(accessorCalls == 1);
    BOOST_TEST(machine.getIntegerResult() == 41);
    machine.setIntermediate(RuntimeValue{});
    BOOST_CHECK_THROW(execute(numberLoad), VirtualMachineException);
    RuntimeValue nativeReceiver;
    nativeReceiver.setStringValue("not script storage");
    machine.setIntermediate(nativeReceiver);
    BOOST_CHECK_THROW(execute(numberStore), VirtualMachineException);
    machine.setIntermediate(RuntimeValue{});
    nativeReceiver.freeObjectValue();
}

BOOST_AUTO_TEST_CASE(specialized_fields_preserve_errors_for_instances_created_before_field_extension) {
    Runtime runtime;
    const auto* owner = createTestType(runtime, "ExtendedFields");
    const auto beforeId = runtime.getTypeManager().registerScriptClassProperty(owner->getId(), "before", runtime.getIntegerType());
    BOOST_REQUIRE(beforeId.isValid());
    ScriptObject object(owner);
    const auto afterId = runtime.getTypeManager().registerScriptClassProperty(owner->getId(), "after", runtime.getIntegerType());
    BOOST_REQUIRE(afterId.isValid());
    auto compiler = runtime.createCompiler();
    const auto address = runtime.getInstructions().size();
    compiler->addLoadProperty(afterId);
    VirtualMachine machine(&runtime);
    RuntimeValue receiver;
    receiver.setObjectValue(&object);
    machine.setIntermediate(receiver);
    machine.jump(address + 1);
    BOOST_CHECK_THROW(runtime.getInstructions()[address].fn(&machine), VirtualMachineException);
    BOOST_TEST(object.getStorageSize() == 1U);
    BOOST_TEST(object.getFieldCount() == 1U);
    machine.setIntermediate(RuntimeValue{});
}

BOOST_AUTO_TEST_SUITE_END()
