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

auto constructScriptObject(VirtualMachine& machine, const ObjectType* type) {
    BOOST_REQUIRE(type->getConstructors().size() == 1u);
    const auto release = [](ScriptObject* object) { object->releaseRef(); };
    return std::unique_ptr<ScriptObject, decltype(release)>(
        static_cast<ScriptObject*>(machine.constructObject(type->getConstructors()[0]->getId(), {})),
        release
    );
}

void compileTestConstructor(
    Runtime& runtime,
    Compiler& compiler,
    const ObjectType* type,
    CallableId initializerId
) {
    const auto id = runtime.registerScriptConstructor(type->getId(), {}, "", "");
    BOOST_REQUIRE(id.isValid());
    auto* constructor = runtime.getScriptCallable(id);
    constructor->setFunctionStart(runtime.getInstructions().size());
    compiler.addFunctionEntry(constructor);
    compiler.addOpCode(OpCodes::op_call_method);
    compiler.addIndex(runtime.getScriptCallable(initializerId)->getFunctionStart());
    compiler.addFunctionReturn(constructor);
    constructor->setFunctionEnd(runtime.getInstructions().size());
}

} // namespace

BOOST_AUTO_TEST_SUITE(language_script_object)

BOOST_AUTO_TEST_CASE(fields_use_owner_local_slots_and_zeroed_raw_storage) {
    Runtime runtime;
    const auto* unrelated = createTestType(runtime, "Unrelated");
    BOOST_REQUIRE(runtime.registerProperty(unrelated->getId(), "other", runtime.getIntegerType()).isValid());
    const auto* type = createTestType(runtime, "Fields");
    const auto nativeId = runtime.addProperty(type->getId(), {
        .valueTypeId = runtime.getTypeId("int"),
        .name = "native",
        .getter = [](VirtualMachine*, RuntimeValue& result) { result.setIntegerValue(42); }
    });
    BOOST_REQUIRE(nativeId.isValid());
    const auto integerId = runtime.registerProperty(type->getId(), "integer", runtime.getIntegerType());
    const auto floatId = runtime.registerProperty(type->getId(), "number", runtime.getFloatType());
    const auto booleanId = runtime.registerProperty(type->getId(), "flag", runtime.getBooleanType());
    const auto stringId = runtime.registerProperty(type->getId(), "text", runtime.getStringType());
    const auto referenceId = runtime.registerProperty(type->getId(), "reference", type);
    for (const auto id : {integerId, floatId, booleanId, stringId, referenceId}) {
        BOOST_REQUIRE(id.isValid());
    }
    ScriptObject object(type);
    BOOST_TEST(object.getObjectType() == type);
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

BOOST_AUTO_TEST_CASE(allocation_and_stores_do_not_run_type_initializers) {
    Runtime runtime;
    auto initialized = NomadIndex{0};
    auto destroyed = NomadIndex{0};
    const auto valueId = runtime.createObjectType<InitializingType>(
        "Counted", ObjectTypeMode::Closed, initialized, destroyed, false
    );
    BOOST_REQUIRE(valueId.isValid());
    const auto* type = createTestType(runtime, "Owner");
    BOOST_REQUIRE(runtime.registerProperty(type->getId(), "value", runtime.getType(valueId).value()).isValid());
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
    BOOST_REQUIRE(runtime.registerProperty(type->getId(), "value", runtime.getIntegerType()).isValid());
    RuntimeValue owner;
    owner.setObjectValue(new ScriptObject(type));
    auto* first = owner.asObject<ScriptObject>();
    ScriptObject second(type);
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
    BOOST_REQUIRE(runtime.registerProperty(type->getId(), "first", type).isValid());
    BOOST_REQUIRE(runtime.registerProperty(type->getId(), "second", type).isValid());
    auto destroyed = NomadIndex{0};
    {
        ScriptObject object(type);
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

BOOST_AUTO_TEST_CASE(partial_out_of_order_stores_release_values_without_initialization_tracking) {
    Runtime runtime;
    auto initialized = NomadIndex{0};
    auto destroyed = NomadIndex{0};
    const auto countedId = runtime.createObjectType<InitializingType>(
        "Counted", ObjectTypeMode::Closed, initialized, destroyed, false
    );
    BOOST_REQUIRE(countedId.isValid());
    const auto* type = createTestType(runtime, "Partial");
    for (auto index = NomadIndex{0}; index < 3; ++index) {
        BOOST_REQUIRE(runtime.registerProperty(type->getId(), "field" + std::to_string(index),
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
    const auto firstId = runtime.registerProperty(type->getId(), "first", runtime.getIntegerType());
    BOOST_REQUIRE(firstId.isValid());
    ScriptObject object(type);
    const auto laterId = runtime.registerProperty(type->getId(), "later", runtime.getFloatType());
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
        BOOST_REQUIRE(runtime.registerProperty(type->getId(), "text", runtime.getStringType()).isValid());
        BOOST_REQUIRE(runtime.registerProperty(type->getId(), "next", type).isValid());
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
    const auto id = runtime.registerProperty(type->getId(), "value", runtime.getStringType());
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
    runtime.getStringType()->freeValue(result);
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
    const auto* type = runtime.getTypeByName("Point")->asObjectType();
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
    const auto* type = runtime.getTypeByName("Owner")->asObjectType();
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
    const auto* type = runtime.getTypeByName("Fields")->asObjectType();
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
    const auto valueId = runtime.createObjectType<InitializingType>(
        "Counted", ObjectTypeMode::Closed, initialized, destroyed, false
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
    const auto* broken = runtime.getTypeByName("Broken")->asObjectType();
    const auto* working = runtime.getTypeByName("Working")->asObjectType();
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

BOOST_AUTO_TEST_CASE(empty_initializers_and_invalid_host_construction_requests) {
    test::LanguageTestFixture fixture;
    fixture.addFunction("main", "class Empty\nend");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    auto machine = runtime.createVirtualMachine();
    const auto* type = runtime.getTypeByName("Empty")->asObjectType();
    const auto empty = constructScriptObject(*machine, type);
    BOOST_TEST(empty->getFieldCount() == 0u);
    BOOST_TEST(empty->getReferenceCount() == 1u);
    BOOST_CHECK_THROW(static_cast<void>(machine->constructObject(CallableId::INVALID, {})), NomadException);
    BOOST_CHECK_THROW(static_cast<void>(machine->constructObject(type->getConstructors()[0]->getId(),
        {RuntimeValue{NomadInteger{1}}})), NomadException);
    const auto* uncompiled = createTestType(runtime, "Uncompiled");
    const auto constructorId = runtime.registerScriptConstructor(uncompiled->getId(), {}, "", "");
    BOOST_REQUIRE(constructorId.isValid());
    BOOST_CHECK_THROW(static_cast<void>(machine->constructObject(constructorId, {})), NomadException);
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
    BOOST_REQUIRE(runtime.registerProperty(type->getId(), "visits", runtime.getIntegerType()).isValid());
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
        BOOST_TEST(object->getReferenceCount() == 1u);
        const auto* initializer = object->getObjectType()->getMethods()[0]->asScriptCallable();
        BOOST_TEST(initializer->getVariableId("this").isInvalid());
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
    compiler->addFunctionEntry(initializer);
    compiler->addOpCode(OpCodes::op_code_load_default_r);
    compiler->addId(runtime.getIntegerType()->getId().id);
    compiler->addOpCode(OpCodes::op_copy_this_to_i);
    compiler->addOpCode(OpCodes::op_native_property_set);
    compiler->addId(type->getProperty("visits")->getId().id);
    compiler->addOpCode(OpCodes::op_object_free_i);
    compiler->addOpCode(probe);
    compiler->addOpCode(OpCodes::op_call_function);
    compiler->addIndex(runtime.getScriptFunction(runtime.getFunctionId("helper"))->getFunctionStart());
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
        const auto& name = object->getObjectType()->getTypeName();
        if (name == "BrokenReceiver") {
            throw NomadException("Receiver frame failed");
        }
        if (name == "OuterReceiver") {
            for (const auto* nestedName : {"InnerReceiver", "BrokenReceiver"}) {
                const auto* nestedType = machine->getRuntime()->getTypeByName(nestedName)->asObjectType();
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
        const auto outer = constructScriptObject(*machine, runtime.getTypeByName("OuterReceiver")->asObjectType());
        BOOST_TEST(outer->getReferenceCount() == 1u);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        BOOST_CHECK_THROW(constructScriptObject(*machine,
            runtime.getTypeByName("BrokenReceiver")->asObjectType()), NomadException);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
        const auto recovered = constructScriptObject(*machine, runtime.getTypeByName("InnerReceiver")->asObjectType());
        BOOST_TEST(recovered->getReferenceCount() == 1u);
        BOOST_CHECK_THROW(static_cast<void>(machine->getThis()), VirtualMachineException);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_SUITE_END()
