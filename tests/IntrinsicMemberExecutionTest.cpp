// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

using namespace nomad;

namespace {

NomadId compileFunction(Runtime& runtime, const NomadString& source) {
    auto compiler = runtime.createCompiler();
    const auto functionId = compiler->registerScriptFile("test", "test.nomad", source);
    CompilerContext context(compiler.get());

    const auto compiled = compiler->compileFunctions(&context);
    const auto* error = context.getFirstError();
    const auto message = error == nullptr ? NomadString("Unknown compilation error") : error->message;
    BOOST_REQUIRE_MESSAGE(compiled, message);
    return functionId;
}

} // namespace

BOOST_AUTO_TEST_SUITE(intrinsic_member_execution)

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

    Interpreter interpreter(&runtime);
    BOOST_REQUIRE(interpreter.execute("value = 4"));
    const auto methodExecuted = interpreter.execute("value.increment 3");
    const auto* error = interpreter.getError();
    const auto message = error == nullptr ? NomadString("Unknown interpreter error") : *error;
    BOOST_REQUIRE_MESSAGE(methodExecuted, message);
    BOOST_REQUIRE(interpreter.execute("value.doubled"));
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

    Interpreter interpreter(&runtime);
    BOOST_REQUIRE(interpreter.execute("value = 4"));
    BOOST_TEST(!interpreter.execute("value.fail"));
    BOOST_REQUIRE(interpreter.getVariableValue("value") != nullptr);
    BOOST_TEST(interpreter.getVariableValue("value")->getIntegerValue() == 4);
}

BOOST_AUTO_TEST_SUITE_END()
