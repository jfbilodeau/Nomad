// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

using namespace nomad;

namespace {

class TestObject : public NomadObject {
public:
    NomadInteger value = 4;
};

const Type* registerTestObject(Runtime& runtime) {
    auto descriptor = std::make_unique<ObjectType>(
        "TestObject", std::vector<const Type*>{}, [] { return new TestObject; }
    );
    const auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
    runtime.registerNativeProperty(
        type, "value", runtime.getIntegerType(),
        [](VirtualMachine* machine, RuntimeValue& result) {
            result.setIntegerValue(machine->getThisAs<TestObject>()->value);
        },
        [](VirtualMachine* machine, const RuntimeValue& value) {
            machine->getThisAs<TestObject>()->value = value.getIntegerValue();
        }, ""
    );
    runtime.registerNativeMethod(
        type, "increment",
        [](VirtualMachine* machine) {
            auto* instance = machine->getThisAs<TestObject>();
            instance->value += machine->getIntegerParameter(0);
            machine->setIntegerResult(instance->value);
        },
        {defParameter("amount", runtime.getIntegerType(), "")}, runtime.getIntegerType(), ""
    );
    return type;
}

NomadId compileFunction(Runtime& runtime, const NomadString& source) {
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"test", {"test.nomad", source}}}, {}});
    const auto functionId = runtime.getFunctionId("test");

    const auto compiled = compiler->compileFunctions(&context);
    const auto* error = context.getFirstError();
    const auto message = error == nullptr ? NomadString("Unknown compilation error") : error->message;
    BOOST_REQUIRE_MESSAGE(compiled, message);
    return functionId;
}

} // namespace

BOOST_AUTO_TEST_SUITE(native_member_execution)

BOOST_AUTO_TEST_CASE(typed_method_borrows_the_string_receiver) {
    Runtime runtime;
    bool called = false;
    runtime.registerNativeMethod(
        runtime.getStringType(),
        "typedLength",
        [&called](VirtualMachine* virtualMachine) {
            const auto* receiver = virtualMachine->getThisAs<const NomadStringObject>();
            called = true;
            BOOST_TEST(receiver == virtualMachine->getThis());
            virtualMachine->setIntegerResult(static_cast<NomadInteger>(receiver->getLength()));
        },
        {},
        runtime.getIntegerType(),
        NomadDoc("Read the string length")
    );

    const auto functionId = compileFunction(runtime, "return \"Nomad\".typedLength");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(called);
    BOOST_TEST(result.getIntegerValue() == 5);
}

BOOST_AUTO_TEST_CASE(typed_method_supports_a_host_defined_object) {
    Runtime runtime;
    const auto* type = registerTestObject(runtime);
    const auto memberId = runtime.registerNativeMethod(
        type,
        "hostObject",
        [](VirtualMachine* virtualMachine) {
            auto* receiver = virtualMachine->getThisAs<TestObject>();
            BOOST_TEST(receiver->getReferenceCount() == 1);
            virtualMachine->setIntegerResult(42);
        },
        {},
        runtime.getIntegerType(),
        NomadDoc("Test a host-defined receiver")
    );
    BOOST_REQUIRE(memberId != NOMAD_INVALID_ID);
    auto virtualMachine = runtime.createVirtualMachine();
    RuntimeValue receiver;
    receiver.setObjectValue(new TestObject);
    virtualMachine->runNativeMethod(memberId, {}, receiver);
    BOOST_TEST(virtualMachine->getResult().getIntegerValue() == 42);
    receiver.freeObjectValue();
}

BOOST_AUTO_TEST_CASE(typed_receiver_requires_an_active_method) {
    Runtime runtime;
    auto virtualMachine = runtime.createVirtualMachine();
    BOOST_CHECK_THROW(
        virtualMachine->getThisAs<NomadStringObject>(),
        VirtualMachineException
    );
    BOOST_CHECK_THROW(virtualMachine->getThisAs<TestObject>(), VirtualMachineException);
}

BOOST_AUTO_TEST_CASE(object_method_borrows_instance_without_receiver_writeback) {
    Runtime runtime;
    auto descriptor = std::make_unique<ObjectType>("TestObject");
    const auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
    auto* instance = new TestObject;
    const auto memberId = runtime.registerNativeMethod(
        type,
        "checkInstance",
        [instance](VirtualMachine* machine) {
            BOOST_TEST(machine->getThis() == instance);
            BOOST_TEST(machine->getThisAs<TestObject>() == instance);
            machine->setIntegerResult(42);
        },
        {},
        runtime.getIntegerType(),
        ""
    );
    BOOST_REQUIRE(memberId != NOMAD_INVALID_ID);
    NativeMethodDefinition definition;
    BOOST_REQUIRE(runtime.getNativeMethodDefinition(memberId, definition));
    auto machine = runtime.createVirtualMachine();
    RuntimeValue receiver;
    receiver.setObjectValue(instance);
    machine->runNativeMethod(memberId, {}, receiver);
    BOOST_TEST(machine->getResult().getIntegerValue() == 42);
    receiver.freeObjectValue();
    BOOST_CHECK_THROW(machine->runNativeMethod(memberId, {}, receiver), VirtualMachineException);
}

BOOST_AUTO_TEST_CASE(native_registration_keeps_methods_out_of_global_function_catalog) {
    Runtime runtime;
    auto descriptor = std::make_unique<ObjectType>("CatalogObject");
    const auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
    const auto first = runtime.registerNativeFunction(
        "catalog.first", [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
    );
    const auto method = runtime.registerNativeMethod(
        type, "catalogMethod", [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
    );
    const auto last = runtime.registerNativeFunction(
        "catalog.last", [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
    );
    BOOST_REQUIRE(method != NOMAD_INVALID_ID);
    BOOST_TEST(last > first + 1);
    BOOST_TEST(runtime.getNextNativeFunctionId(first) == last);
    BOOST_TEST(runtime.getNativeFunctionId("catalogMethod") == NOMAD_INVALID_ID);
    BOOST_TEST(runtime.getNativeMemberId(type, "catalogMethod") == method);
    NativeMethodDefinition methodDefinition;
    BOOST_REQUIRE(runtime.getNativeMethodDefinition(method, methodDefinition));
    BOOST_TEST(methodDefinition.id == method);
    BOOST_TEST(methodDefinition.objectType == type);
    std::vector<NativeFunctionDefinition> functions;
    runtime.getNativeFunctions(functions);
    NomadIndex enumerated = 0;
    for (auto id = runtime.getFirstNativeFunctionId(); id != NOMAD_INVALID_ID;
         id = runtime.getNextNativeFunctionId(id)) {
        NativeFunctionDefinition definition;
        BOOST_REQUIRE(runtime.getNativeFunctionDefinition(id, definition));
        BOOST_TEST(definition.objectType == nullptr);
        BOOST_TEST(definition.name != "catalogMethod");
        ++enumerated;
    }
    BOOST_TEST(enumerated == functions.size());
    for (const auto& definition : functions) {
        BOOST_TEST(definition.objectType == nullptr);
        BOOST_TEST(definition.name != "catalogMethod");
    }
    BOOST_TEST(runtime.registerNativeMethod(
        nullptr, "invalid", [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
    ) == NOMAD_INVALID_ID);
}

BOOST_AUTO_TEST_CASE(compiled_property_getter_reads_temporary_receiver) {
    Runtime runtime;

    const auto functionId = compileFunction(runtime, "return \"Nomad\".length");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 5);
}

BOOST_AUTO_TEST_CASE(compiled_property_setter_commits_mutated_receiver) {
    Runtime runtime;
    registerTestObject(runtime);

    const auto functionId = compileFunction(runtime,
        "object = new TestObject\nalias = object\nobject.value = \"abc\".length\nreturn alias.value");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 3);
}

BOOST_AUTO_TEST_CASE(compiled_method_receives_receiver_and_arguments) {
    Runtime runtime;
    registerTestObject(runtime);

    const auto functionId = compileFunction(runtime, "object = new TestObject\nreturn object.increment \"abc\".length");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 7);
}

BOOST_AUTO_TEST_CASE(compiled_properties_and_methods_mutate_parameter_instances) {
    Runtime runtime;
    registerTestObject(runtime);

    const auto functionId = compileFunction(runtime,
        "fun change object:TestObject\n"
        " object.value = 10\n"
        " object.increment 3\n"
        "end\n"
        "object = new TestObject\nchange object\nreturn object.value");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 13);
}

BOOST_AUTO_TEST_CASE(interpreter_executes_properties_and_methods) {
    Runtime runtime;
    registerTestObject(runtime);

    auto machine = runtime.createVirtualMachine();
    InterpreterContext interpreter(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("object = new TestObject", interpreter));
    BOOST_REQUIRE(Interpreter{}.execute("object.value = \"abc\".length", interpreter));
    const auto methodExecuted = Interpreter{}.execute("object.increment 3", interpreter);
    const auto* error = interpreter.getError();
    const auto message = error == nullptr ? NomadString("Unknown interpreter error") : *error;
    BOOST_REQUIRE_MESSAGE(methodExecuted, message);
    BOOST_REQUIRE(Interpreter{}.execute("object.value", interpreter));
    BOOST_REQUIRE(interpreter.getResult() != nullptr);

    BOOST_TEST(interpreter.getResult()->getValue().getIntegerValue() == 6);
    BOOST_CHECK_THROW(machine->getThis(), VirtualMachineException);
}

BOOST_AUTO_TEST_CASE(failed_method_preserves_object_mutations_and_restores_this) {
    Runtime runtime;
    const auto* type = registerTestObject(runtime);
    runtime.registerNativeMethod(
        type,
        "fail",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->getThisAs<TestObject>()->value = 99;
            virtualMachine->fault("Expected failure");
        },
        {},
        runtime.getVoidType(),
        NomadDoc("Fail")
    );

    auto machine = runtime.createVirtualMachine();
    InterpreterContext interpreter(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("object = new TestObject", interpreter));
    BOOST_TEST(!Interpreter{}.execute("object.fail", interpreter));
    BOOST_REQUIRE(interpreter.getVariableValue("object") != nullptr);
    BOOST_TEST(static_cast<TestObject*>(interpreter.getVariableValue("object")->getObjectValue())->value == 99);
    BOOST_CHECK_THROW(machine->getThis(), VirtualMachineException);
}

BOOST_AUTO_TEST_SUITE_END()
