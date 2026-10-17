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
};

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

BOOST_AUTO_TEST_SUITE(intrinsic_member_execution)

BOOST_AUTO_TEST_CASE(typed_method_borrows_the_string_receiver) {
    Runtime runtime;
    bool called = false;
    runtime.registerIntrinsicMethod(
        runtime.getStringType(),
        "typedLength",
        [&called](VirtualMachine* virtualMachine) {
            const auto* receiver = virtualMachine->getIntrinsicReceiverAs<const NomadStringObject>();
            called = true;
            BOOST_TEST(receiver == virtualMachine->getIntrinsicReceiver().getStringObject());
            virtualMachine->setIntegerResult(static_cast<NomadInteger>(receiver->getLength()));
        },
        {},
        runtime.getIntegerType(),
        false,
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
    const auto memberId = runtime.registerIntrinsicMethod(
        runtime.getStringType(),
        "hostObject",
        [](VirtualMachine* virtualMachine) {
            auto* receiver = virtualMachine->getIntrinsicReceiverAs<TestObject>();
            BOOST_TEST(receiver->getReferenceCount() == 1);
            virtualMachine->setIntegerResult(42);
        },
        {},
        runtime.getIntegerType(),
        false,
        NomadDoc("Test a host-defined receiver")
    );
    BOOST_REQUIRE(memberId != NOMAD_INVALID_ID);
    auto virtualMachine = runtime.createVirtualMachine();
    RuntimeValue receiver;
    receiver.setObjectValue(new TestObject);
    virtualMachine->runIntrinsicMethod(memberId, {}, receiver);
    BOOST_TEST(virtualMachine->getResult().getIntegerValue() == 42);
    receiver.freeObjectValue();
}

BOOST_AUTO_TEST_CASE(typed_receiver_requires_an_active_method) {
    Runtime runtime;
    auto virtualMachine = runtime.createVirtualMachine();
    BOOST_CHECK_THROW(
        virtualMachine->getIntrinsicReceiverAs<NomadStringObject>(),
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
    const auto memberId = runtime.registerObjectMethod(
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
    IntrinsicMethodDefinition definition;
    BOOST_REQUIRE(runtime.getIntrinsicMethodDefinition(memberId, definition));
    BOOST_TEST(!definition.mutatesReceiver);
    auto machine = runtime.createVirtualMachine();
    RuntimeValue receiver;
    receiver.setObjectValue(instance);
    machine->runIntrinsicMethod(memberId, {}, receiver);
    BOOST_TEST(machine->getResult().getIntegerValue() == 42);
    receiver.freeObjectValue();
    BOOST_CHECK_THROW(machine->runIntrinsicMethod(memberId, {}, receiver), VirtualMachineException);
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
    runtime.registerIntrinsicProperty(
        runtime.getIntegerType(),
        "scaled",
        runtime.getIntegerType(),
        {},
        [](VirtualMachine*, RuntimeValue& receiver, const RuntimeValue& value) {
            receiver.setIntegerValue(receiver.getIntegerValue() * value.getIntegerValue());
        },
        NomadDoc("Scale receiver")
    );

    const auto functionId = compileFunction(runtime, "value = 4\nvalue.scaled = 3\nreturn value");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 12);
}

BOOST_AUTO_TEST_CASE(compiled_method_receives_receiver_and_arguments) {
    Runtime runtime;
    runtime.registerIntrinsicMethod(
        runtime.getIntegerType(),
        "plus",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->setIntegerResult(
                virtualMachine->getIntrinsicReceiver().getIntegerValue() +
                virtualMachine->getIntegerParameter(0)
            );
        },
        {defParameter("value", runtime.getIntegerType(), NomadDoc("Value"))},
        runtime.getIntegerType(),
        false,
        NomadDoc("Add value")
    );

    const auto functionId = compileFunction(runtime, "return (4).plus 3");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 7);
}

BOOST_AUTO_TEST_CASE(compiled_mutating_method_commits_receiver) {
    Runtime runtime;
    runtime.registerIntrinsicMethod(
        runtime.getIntegerType(),
        "increment",
        [](VirtualMachine* virtualMachine) {
            auto& receiver = virtualMachine->getIntrinsicReceiver();
            receiver.setIntegerValue(receiver.getIntegerValue() + virtualMachine->getIntegerParameter(0));
        },
        {defParameter("value", runtime.getIntegerType(), NomadDoc("Value"))},
        runtime.getVoidType(),
        true,
        NomadDoc("Increment receiver")
    );

    const auto functionId = compileFunction(runtime, "value = 4\nvalue.increment 3\nreturn value");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 7);
}

BOOST_AUTO_TEST_CASE(interpreter_executes_properties_and_methods) {
    Runtime runtime;
    runtime.registerIntrinsicProperty(
        runtime.getIntegerType(),
        "doubled",
        runtime.getIntegerType(),
        [](VirtualMachine*, const RuntimeValue& receiver, RuntimeValue& result) {
            result.setIntegerValue(receiver.getIntegerValue() * 2);
        },
        {},
        NomadDoc("Double receiver")
    );
    runtime.registerIntrinsicMethod(
        runtime.getIntegerType(),
        "increment",
        [](VirtualMachine* virtualMachine) {
            auto& receiver = virtualMachine->getIntrinsicReceiver();
            receiver.setIntegerValue(receiver.getIntegerValue() + virtualMachine->getIntegerParameter(0));
        },
        {defParameter("value", runtime.getIntegerType(), NomadDoc("Value"))},
        runtime.getVoidType(),
        true,
        NomadDoc("Increment receiver")
    );

    auto machine = runtime.createVirtualMachine();
    InterpreterContext interpreter(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("value = 4", interpreter));
    const auto methodExecuted = Interpreter{}.execute("value.increment 3", interpreter);
    const auto* error = interpreter.getError();
    const auto message = error == nullptr ? NomadString("Unknown interpreter error") : *error;
    BOOST_REQUIRE_MESSAGE(methodExecuted, message);
    BOOST_REQUIRE(Interpreter{}.execute("value.doubled", interpreter));
    BOOST_REQUIRE(interpreter.getResult() != nullptr);

    BOOST_TEST(interpreter.getResult()->getValue().getIntegerValue() == 14);
    BOOST_REQUIRE(interpreter.getVariableValue("value") != nullptr);
    BOOST_TEST(interpreter.getVariableValue("value")->getIntegerValue() == 7);
}

BOOST_AUTO_TEST_CASE(failed_mutating_method_does_not_commit_receiver) {
    Runtime runtime;
    runtime.registerIntrinsicMethod(
        runtime.getIntegerType(),
        "fail",
        [](VirtualMachine* virtualMachine) {
            virtualMachine->getIntrinsicReceiver().setIntegerValue(99);
            virtualMachine->fault("Expected failure");
        },
        {},
        runtime.getVoidType(),
        true,
        NomadDoc("Fail")
    );

    auto machine = runtime.createVirtualMachine();
    InterpreterContext interpreter(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("value = 4", interpreter));
    BOOST_TEST(!Interpreter{}.execute("value.fail", interpreter));
    BOOST_REQUIRE(interpreter.getVariableValue("value") != nullptr);
    BOOST_TEST(interpreter.getVariableValue("value")->getIntegerValue() == 4);
}

BOOST_AUTO_TEST_SUITE_END()
