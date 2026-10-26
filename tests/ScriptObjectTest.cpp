// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <string>
#include <type_traits>
#include <utility>

using namespace nomad;

static_assert(!std::is_copy_constructible_v<ScriptObject>);
static_assert(!std::is_copy_assignable_v<ScriptObject>);

namespace {

const ObjectType* createTestType(Runtime& runtime, const NomadString& name) {
    const auto id = runtime.createObjectType(name, ObjectTypeMode::Opened);
    BOOST_REQUIRE(id.isValid());
    return runtime.getType(id).value()->asObjectType();
}

class CountingObject final : public NomadObject {
public:
    explicit CountingObject(NomadIndex& destroyed): m_destroyed(destroyed) {}
    ~CountingObject() override { ++m_destroyed; }

private:
    NomadIndex& m_destroyed;
};

class InitializingType final : public ObjectType {
public:
    InitializingType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ObjectTypeMode mode,
        NomadIndex& initialized,
        NomadIndex& destroyed,
        bool fail
    ):
        ObjectType(runtime, id, std::move(name), mode),
        m_initialized(initialized),
        m_destroyed(destroyed),
        m_fail(fail)
    {}

    void initValue(RuntimeValue& value) const override {
        ++m_initialized;
        value.setObjectValue(new CountingObject(m_destroyed));
        if (m_fail) {
            throw NomadException("Field initialization failed");
        }
    }

private:
    NomadIndex& m_initialized;
    NomadIndex& m_destroyed;
    bool m_fail;
};

} // namespace

BOOST_AUTO_TEST_SUITE(language_script_object)

BOOST_AUTO_TEST_CASE(fields_use_owner_local_slots_and_type_defaults) {
    Runtime runtime;
    const auto* unrelated = createTestType(runtime, "Unrelated");
    BOOST_REQUIRE(runtime.registerProperty(unrelated, "other", runtime.getIntegerType()).isValid());
    const auto* type = createTestType(runtime, "Fields");
    const auto nativeId = runtime.addProperty(type->getId(), {
        .valueTypeId = runtime.getTypeId("int"),
        .name = "native",
        .getter = [](VirtualMachine*, RuntimeValue& result) { result.setIntegerValue(42); }
    });
    BOOST_REQUIRE(nativeId.isValid());
    const auto integerId = runtime.registerProperty(type, "integer", runtime.getIntegerType());
    const auto floatId = runtime.registerProperty(type, "number", runtime.getFloatType());
    const auto booleanId = runtime.registerProperty(type, "flag", runtime.getBooleanType());
    const auto stringId = runtime.registerProperty(type, "text", runtime.getStringType());
    const auto referenceId = runtime.registerProperty(type, "reference", type);
    for (const auto id : {integerId, floatId, booleanId, stringId, referenceId}) {
        BOOST_REQUIRE(id.isValid());
    }
    ScriptObject object(type);
    BOOST_TEST(object.getObjectType() == type);
    BOOST_TEST(object.getFieldCount() == 5u);
    BOOST_TEST(type->getFieldIndex(nativeId) == NOMAD_INVALID_INDEX);
    BOOST_TEST(type->getFieldIndex(integerId) == 0u);
    BOOST_TEST(type->getFieldIndex(referenceId) == 4u);
    object.initializeFields();
    BOOST_TEST(object.getField(type->getFieldIndex(integerId)).getIntegerValue() == 0);
    BOOST_TEST(object.getField(type->getFieldIndex(floatId)).getFloatValue() == 0.0);
    BOOST_TEST(!object.getField(type->getFieldIndex(booleanId)).getBooleanValue());
    BOOST_TEST(NomadString(object.getField(type->getFieldIndex(stringId)).getStringValue()).empty());
    BOOST_TEST(object.getField(type->getFieldIndex(referenceId)).getObjectValue() == nullptr);
}

BOOST_AUTO_TEST_CASE(allocation_does_not_initialize_and_defaults_run_once) {
    Runtime runtime;
    auto initialized = NomadIndex{0};
    auto destroyed = NomadIndex{0};
    const auto valueId = runtime.createObjectType<InitializingType>(
        "Counted", ObjectTypeMode::Closed, initialized, destroyed, false
    );
    BOOST_REQUIRE(valueId.isValid());
    const auto* type = createTestType(runtime, "Owner");
    BOOST_REQUIRE(runtime.registerProperty(type, "value", runtime.getType(valueId).value()).isValid());
    {
        ScriptObject uninitialized(type);
        BOOST_TEST(initialized == 0u);
        BOOST_CHECK_THROW((void)uninitialized.getField(0), NomadException);
        BOOST_CHECK_THROW(uninitialized.setField(0, RuntimeValue{}), NomadException);
    }
    BOOST_TEST(destroyed == 0u);
    {
        ScriptObject first(type);
        ScriptObject second(type);
        first.initializeFields();
        second.initializeFields();
        BOOST_TEST(initialized == 2u);
        BOOST_TEST(first.getField(0).getObjectValue() != second.getField(0).getObjectValue());
        BOOST_CHECK_THROW(first.initializeFields(), NomadException);
        BOOST_TEST(initialized == 2u);
    }
    BOOST_TEST(destroyed == 2u);
}

BOOST_AUTO_TEST_CASE(instances_have_independent_values_and_aliases_share_the_same_instance) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Point");
    BOOST_REQUIRE(runtime.registerProperty(type, "value", runtime.getIntegerType()).isValid());
    RuntimeValue owner;
    owner.setObjectValue(new ScriptObject(type));
    auto* first = owner.asObject<ScriptObject>();
    first->initializeFields();
    ScriptObject second(type);
    second.initializeFields();
    RuntimeValue alias;
    type->copyValue(owner, alias);
    BOOST_TEST(first->getReferenceCount() == 2u);
    alias.asObject<ScriptObject>()->setField(0, RuntimeValue(NomadInteger{17}));
    BOOST_TEST(first->getField(0).getIntegerValue() == 17);
    BOOST_TEST(second.getField(0).getIntegerValue() == 0);
    type->freeValue(owner);
    BOOST_TEST(alias.asObject<ScriptObject>()->getField(0).getIntegerValue() == 17);
    type->freeValue(alias);
}

BOOST_AUTO_TEST_CASE(field_replacement_retains_references_and_handles_self_assignment) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Owner");
    BOOST_REQUIRE(runtime.registerProperty(type, "first", type).isValid());
    BOOST_REQUIRE(runtime.registerProperty(type, "second", type).isValid());
    auto destroyed = NomadIndex{0};
    {
        ScriptObject object(type);
        object.initializeFields();
        RuntimeValue value;
        value.setObjectValue(new CountingObject(destroyed));
        object.setField(0, value);
        object.setField(1, object.getField(0));
        object.setField(0, object.getField(0));
        BOOST_TEST(value.getObjectValue()->getReferenceCount() == 3u);
        type->freeValue(value);
        object.setField(0, RuntimeValue{});
        BOOST_TEST(destroyed == 0u);
        BOOST_TEST(object.getField(1).getObjectValue()->getReferenceCount() == 1u);
        object.setField(1, RuntimeValue{});
        BOOST_TEST(destroyed == 1u);
        RuntimeValue remaining;
        remaining.setObjectValue(new CountingObject(destroyed));
        object.setField(0, remaining);
        type->freeValue(remaining);
    }
    BOOST_TEST(destroyed == 2u);
}

BOOST_AUTO_TEST_CASE(partial_initialization_releases_completed_and_throwing_fields) {
    Runtime runtime;
    auto initialized = NomadIndex{0};
    auto destroyed = NomadIndex{0};
    const auto countedId = runtime.createObjectType<InitializingType>(
        "Counted", ObjectTypeMode::Closed, initialized, destroyed, false
    );
    const auto failingId = runtime.createObjectType<InitializingType>(
        "Failing", ObjectTypeMode::Closed, initialized, destroyed, true
    );
    BOOST_REQUIRE(countedId.isValid());
    BOOST_REQUIRE(failingId.isValid());
    for (auto failingIndex = NomadIndex{0}; failingIndex < 3; ++failingIndex) {
        const auto* type = createTestType(runtime, "Failure" + std::to_string(failingIndex));
        for (auto index = NomadIndex{0}; index < 3; ++index) {
            const auto* fieldType = runtime.getType(index == failingIndex ? failingId : countedId).value();
            BOOST_REQUIRE(runtime.registerProperty(type, "field" + std::to_string(index), fieldType).isValid());
        }
        const auto before = initialized;
        {
            ScriptObject object(type);
            BOOST_CHECK_THROW(object.initializeFields(), NomadException);
            BOOST_TEST(initialized == before + failingIndex + 1);
            BOOST_CHECK_THROW((void)object.getField(0), NomadException);
            BOOST_CHECK_THROW(object.setField(0, RuntimeValue{}), NomadException);
            BOOST_CHECK_THROW(object.initializeFields(), NomadException);
        }
        BOOST_TEST(destroyed == initialized);
    }
}

BOOST_AUTO_TEST_CASE(layout_does_not_resize_and_invalid_access_is_rejected) {
    Runtime runtime;
    BOOST_CHECK_THROW(ScriptObject(nullptr), NomadException);
    const auto* type = createTestType(runtime, "Layout");
    const auto firstId = runtime.registerProperty(type, "first", runtime.getIntegerType());
    BOOST_REQUIRE(firstId.isValid());
    ScriptObject object(type);
    object.initializeFields();
    const auto laterId = runtime.registerProperty(type, "later", runtime.getFloatType());
    BOOST_REQUIRE(laterId.isValid());
    BOOST_TEST(object.getFieldCount() == 1u);
    BOOST_TEST(object.getField(0).getIntegerValue() == 0);
    BOOST_CHECK_THROW((void)object.getField(type->getFieldIndex(laterId)), NomadException);
    BOOST_CHECK_THROW(object.setField(1, RuntimeValue{}), NomadException);
    BOOST_CHECK_THROW((void)object.getField(NOMAD_INVALID_INDEX), NomadException);
    const auto* emptyType = createTestType(runtime, "Empty");
    ScriptObject empty(emptyType);
    BOOST_TEST(empty.getFieldCount() == 0u);
    empty.initializeFields();
    BOOST_CHECK_THROW(empty.initializeFields(), NomadException);
    BOOST_CHECK_THROW((void)empty.getField(0), NomadException);
}

BOOST_AUTO_TEST_CASE(compiled_and_interpreted_properties_share_storage_and_preserve_ownership) {
    for (const auto interpreted : {false, true}) {
        Runtime runtime;
        const auto* type = createTestType(runtime, "Stored");
        BOOST_REQUIRE(runtime.registerProperty(type, "text", runtime.getStringType()).isValid());
        BOOST_REQUIRE(runtime.registerProperty(type, "next", type).isValid());
        BOOST_REQUIRE(runtime.addProperty(type->getId(), {
            .valueTypeId = runtime.getIntegerType()->getId(),
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
                object->initializeFields();
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
                InterpreterContext context(*machine);
                for (auto start = NomadString::size_type{0}; start < source.size();) {
                    const auto end = source.find('\n', start);
                    const auto line = source.substr(start, end - start);
                    const auto executed = Interpreter{}.execute(line, context);
                    BOOST_REQUIRE_MESSAGE(executed,
                        line + ": " + (context.getError() != nullptr ? *context.getError() : "No error reported"));
                    start = end + 1;
                }
                BOOST_REQUIRE(Interpreter{}.execute("first.next.text", context));
                BOOST_REQUIRE(context.getResult() != nullptr);
                BOOST_TEST(NomadString(context.getResult()->getValue().getStringValue()) == "hello");
                BOOST_REQUIRE(Interpreter{}.execute("second.nativeLength", context));
                BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 5);
                BOOST_TEST(!Interpreter{}.execute("second.nativeLength = 2", context));
                BOOST_TEST(!Interpreter{}.execute("second.missing = 2", context));
                BOOST_TEST(type->getProperty("missing") == nullptr);
                BOOST_TEST(!Interpreter{}.execute("second.text = 2", context));
                BOOST_TEST(!Interpreter{}.execute("second.next.text", context));
                BOOST_REQUIRE(context.getError() != nullptr);
                BOOST_REQUIRE(Interpreter{}.execute("alias.text", context));
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
                runtime.getStringType()->freeValue(result);
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
    const auto id = runtime.registerProperty(type, "value", runtime.getStringType());
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
    BOOST_CHECK_THROW(machine->getProperty(id, receiver, result), NomadException);
    BOOST_CHECK_THROW(machine->setProperty(id, receiver, result), NomadException);
    object.initializeFields();
    BOOST_CHECK_THROW(machine->getProperty(PropertyId::INVALID, receiver, result), NomadException);
    BOOST_CHECK_THROW(machine->setProperty(PropertyId::INVALID, receiver, result), NomadException);
    machine->getProperty(id, receiver, result);
    BOOST_TEST(NomadString(result.getStringValue()).empty());
    runtime.getStringType()->freeValue(result);
}

BOOST_AUTO_TEST_CASE(generated_initializer_runs_in_source_order_after_all_type_defaults) {
    test::LanguageTestFixture fixture;
    fixture.addFunction("main",
        "open class Point\n"
        "first = this.second\n"
        "second = 42\n"
        "third = this.second + 1\n"
        "description = $\"point {this.third}\"\n"
        "end\n"
        "fun extend point:Point enabled:bool\n"
        "if enabled\npoint.conditional = 7\nend\nend"
    );
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto* type = runtime.getTypeByName("Point")->asObjectType();
    const auto before = NomadObject::getLiveObjectCount();
    {
        ScriptObject object(type);
        runtime.initializeObject(object);
        BOOST_TEST(object.getField(type->getFieldIndex(type->getProperty("first")->getId())).getIntegerValue() == 0);
        BOOST_TEST(object.getField(type->getFieldIndex(type->getProperty("second")->getId())).getIntegerValue() == 42);
        BOOST_TEST(object.getField(type->getFieldIndex(type->getProperty("third")->getId())).getIntegerValue() == 43);
        BOOST_TEST(object.getField(type->getFieldIndex(type->getProperty("conditional")->getId())).getIntegerValue() == 0);
        BOOST_TEST(NomadString(object.getField(
            type->getFieldIndex(type->getProperty("description")->getId())).getStringValue()) == "point 43");
        BOOST_TEST(object.getReferenceCount() == 1u);
        BOOST_CHECK_THROW(runtime.initializeObject(object), NomadException);
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
    const auto* type = runtime.getTypeByName("Owner")->asObjectType();
    {
        ScriptObject first(type);
        ScriptObject second(type);
        runtime.initializeObject(first);
        runtime.initializeObject(second);
        BOOST_TEST(created == 2u);
        BOOST_TEST(first.getField(0).getObjectValue() != second.getField(0).getObjectValue());
        BOOST_TEST(first.getField(0).getObjectValue()->getReferenceCount() == 1u);
        BOOST_TEST(second.getField(0).getObjectValue()->getReferenceCount() == 1u);
        BOOST_CHECK_THROW(runtime.initializeObject(first), NomadException);
        BOOST_TEST(created == 2u);
    }
    BOOST_TEST(destroyed == 2u);
}

BOOST_AUTO_TEST_CASE(initializer_failure_preserves_cleanup_and_vm_recovery_without_retry) {
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
    const auto* broken = runtime.getTypeByName("Broken")->asObjectType();
    const auto* working = runtime.getTypeByName("Working")->asObjectType();
    const auto before = NomadObject::getLiveObjectCount();
    {
        ScriptObject object(broken);
        BOOST_CHECK_THROW(runtime.initializeObject(object), NomadException);
        BOOST_TEST(object.getReferenceCount() == 1u);
        BOOST_TEST(calls == 1u);
        BOOST_CHECK_THROW(runtime.initializeObject(object), NomadException);
        BOOST_TEST(calls == 1u);
        ScriptObject recovered(working);
        runtime.initializeObject(recovered);
        BOOST_TEST(recovered.getField(0).getIntegerValue() == 42);
    }
    BOOST_TEST(destroyed == 1u);
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(empty_initializers_and_invalid_host_initialization_requests) {
    test::LanguageTestFixture fixture;
    fixture.addFunction("main", "class Empty\nend");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    ScriptObject empty(runtime.getTypeByName("Empty")->asObjectType());
    runtime.initializeObject(empty);
    BOOST_TEST(empty.getFieldCount() == 0u);
    BOOST_TEST(empty.getReferenceCount() == 1u);
    Runtime other;
    BOOST_CHECK_THROW(other.initializeObject(empty), NomadException);
    ScriptObject native(createTestType(runtime, "Native"));
    BOOST_CHECK_THROW(runtime.initializeObject(native), NomadException);
    native.initializeFields();
    BOOST_TEST(native.getFieldCount() == 0u);
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
    BOOST_REQUIRE(runtime.registerProperty(type, "visits", runtime.getIntegerType()).isValid());
    const auto initializerId = runtime.registerScriptMethod(type->getId(), "Receiver$init", {}, "", "");
    BOOST_REQUIRE(initializerId.isValid());
    auto* initializer = runtime.getScriptCallable(initializerId);
    const auto propertyId = runtime.addProperty(type->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
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
                machine->callNativeFunction(CallableId::INVALID), VirtualMachineException
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
        BOOST_TEST(object->getObjectType()->getTypeName() == "Receiver");
        BOOST_TEST(object->getReferenceCount() == 2u);
        const auto* initializer = object->getObjectType()->getMethods()[0]->asScriptCallable();
        BOOST_TEST(machine->getFunctionVariableValue(initializer->getVariableId("this")).getObjectValue() == object);
        RuntimeValue receiver;
        receiver.setObjectValue(object);
        RuntimeValue result;
        machine->getProperty(object->getObjectType()->getProperty("native")->getId(), receiver, result);
        BOOST_TEST(result.getIntegerValue() == 42);
        BOOST_TEST(machine->getThis() == object);
        object->setField(0, RuntimeValue{object->getField(0).getIntegerValue() + 1});
    }, "", {});
    auto compiler = runtime.createCompiler();
    initializer->setFunctionStart(runtime.getInstructions().size());
    compiler->addOpCode(OpCodes::op_function_enter);
    compiler->addId(initializer->getId().id);
    compiler->addOpCode(probe);
    compiler->addOpCode(OpCodes::op_call_function);
    compiler->addIndex(runtime.getScriptFunction(runtime.getFunctionId("helper"))->getFunctionStart());
    compiler->addOpCode(probe);
    compiler->addFunctionReturn(initializer);
    initializer->setFunctionEnd(runtime.getInstructions().size());
    const auto before = NomadObject::getLiveObjectCount();
    {
        ScriptObject object(type);
        auto machine = runtime.createVirtualMachine();
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        machine->initializeObject(object);
        BOOST_TEST(object.getField(0).getIntegerValue() == 2);
        BOOST_TEST(object.getReferenceCount() == 1u);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(script_receiver_restores_after_nested_initialization_and_failure) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto probe = runtime.registerInstruction("test.nestedReceiver", [](VirtualMachine* machine) {
        auto* object = machine->getThisAs<ScriptObject>();
        BOOST_TEST(object->getReferenceCount() == 2u);
        const auto& name = object->getObjectType()->getTypeName();
        if (name == "BrokenReceiver") {
            throw NomadException("Receiver frame failed");
        }
        if (name == "OuterReceiver") {
            for (const auto* nestedName : {"InnerReceiver", "BrokenReceiver"}) {
                ScriptObject nested(machine->getRuntime()->getTypeByName(nestedName)->asObjectType());
                if (NomadString(nestedName) == "BrokenReceiver") {
                    BOOST_CHECK_THROW(machine->initializeObject(nested), NomadException);
                } else {
                    machine->initializeObject(nested);
                }
                BOOST_TEST(nested.getReferenceCount() == 1u);
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
        compiler->addOpCode(OpCodes::op_function_enter);
        compiler->addId(initializer->getId().id);
        compiler->addOpCode(probe);
        compiler->addFunctionReturn(initializer);
        initializer->setFunctionEnd(runtime.getInstructions().size());
    }
    const auto before = NomadObject::getLiveObjectCount();
    {
        auto machine = runtime.createVirtualMachine();
        ScriptObject outer(runtime.getTypeByName("OuterReceiver")->asObjectType());
        machine->initializeObject(outer);
        BOOST_TEST(outer.getReferenceCount() == 1u);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        ScriptObject broken(runtime.getTypeByName("BrokenReceiver")->asObjectType());
        BOOST_CHECK_THROW(machine->initializeObject(broken), NomadException);
        BOOST_TEST(broken.getReferenceCount() == 1u);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        ScriptObject recovered(runtime.getTypeByName("InnerReceiver")->asObjectType());
        machine->initializeObject(recovered);
        BOOST_TEST(recovered.getReferenceCount() == 1u);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_SUITE_END()
