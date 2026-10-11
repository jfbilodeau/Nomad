// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>

using namespace nomad;

namespace {

void registerStringLength(Runtime& runtime) {
    runtime.registerIntrinsicProperty(
        runtime.getStringType(),
        "length",
        runtime.getIntegerType(),
        [](VirtualMachine*, const RuntimeValue&, RuntimeValue&) {},
        {},
        NomadDoc("Length")
    );
}

} // namespace

BOOST_AUTO_TEST_SUITE(intrinsic_member_conflicts)

BOOST_AUTO_TEST_CASE(compiled_function_rejects_existing_qualified_identifier) {
    Runtime runtime;
    registerStringLength(runtime);

    auto compiler = runtime.createCompiler();
    compiler->registerScriptFile(
        "test",
        "test.nomad",
        "name.length = 10\n"
        "name = \"J-F\"\n"
        "return name"
    );
    CompilerContext context(compiler.get());

    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->message.find("name.length") != NomadString::npos);
    BOOST_TEST(context.getFirstError()->message.find("conflicts with an existing identifier") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(interpreter_rejects_existing_qualified_identifier) {
    Runtime runtime;
    registerStringLength(runtime);
    Interpreter interpreter(&runtime);

    BOOST_REQUIRE(interpreter.execute("name.length = 10"));
    BOOST_TEST(!interpreter.execute("name = \"J-F\""));
    BOOST_REQUIRE(interpreter.getError() != nullptr);
    BOOST_TEST(interpreter.getError()->find("name.length") != NomadString::npos);
    BOOST_TEST(interpreter.getError()->find("conflicts with an existing identifier") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(unrelated_qualified_identifier_does_not_conflict) {
    Runtime runtime;
    registerStringLength(runtime);

    auto compiler = runtime.createCompiler();
    const auto functionId = compiler->registerScriptFile(
        "test",
        "test.nomad",
        "name.other = 10\n"
        "name = \"J-F\"\n"
        "return name"
    );
    CompilerContext context(compiler.get());

    BOOST_REQUIRE(compiler->compileFunctions(&context));

    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);
    BOOST_TEST(result.getStringValue() == "J-F");
    runtime.getStringType()->freeValue(result);
}

BOOST_AUTO_TEST_SUITE_END()
