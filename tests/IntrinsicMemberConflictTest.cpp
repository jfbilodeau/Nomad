// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

using namespace nomad;

BOOST_AUTO_TEST_SUITE(intrinsic_member_conflicts)

BOOST_AUTO_TEST_CASE(compiled_function_rejects_existing_qualified_identifier) {
    Runtime runtime;

    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerScriptFile(
        &context,
        "test",
        "test.nomad",
        "name.length = 10\n"
        "name = \"J-F\"\n"
        "return name"
    );

    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->message.find("name.length") != NomadString::npos);
    BOOST_TEST(context.getFirstError()->message.find("conflicts with an existing identifier") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(interpreter_rejects_creating_unknown_qualified_identifier) {
    Runtime runtime;
    auto machine = runtime.createVirtualMachine();
    InterpreterContext interpreter(*machine);

    BOOST_TEST(!Interpreter{}.execute("name.length = 10", interpreter));
    BOOST_REQUIRE(interpreter.getError() != nullptr);
    BOOST_CHECK_MESSAGE(interpreter.getError()->find("name.length") != NomadString::npos, *interpreter.getError());
    BOOST_CHECK_MESSAGE(interpreter.getError()->find("Cannot assign") != NomadString::npos, *interpreter.getError());
    BOOST_TEST(interpreter.getVariableValue("name.length") == nullptr);
    const auto succeeded = Interpreter{}.execute("name = \"J-F\"", interpreter);
    const auto diagnostic = interpreter.getError() ? *interpreter.getError() : NomadString{};
    BOOST_REQUIRE_MESSAGE(succeeded, diagnostic);
}

BOOST_AUTO_TEST_CASE(unrelated_qualified_identifier_does_not_conflict) {
    Runtime runtime;

    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const auto functionId = compiler->registerScriptFile(
        &context,
        "test",
        "test.nomad",
        "name.other = 10\n"
        "name = \"J-F\"\n"
        "return name"
    );

    BOOST_REQUIRE(compiler->compileFunctions(&context));

    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);
    BOOST_TEST(result.getStringValue() == "J-F");
    runtime.getStringType()->freeValue(result);
}

BOOST_AUTO_TEST_SUITE_END()
