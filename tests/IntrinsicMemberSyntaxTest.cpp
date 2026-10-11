// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Function.hpp>
#include <nomad/script/Runtime.hpp>

using namespace nomad;

namespace {

IntrinsicPropertySetFn propertySetter() {
    return [](VirtualMachine*, RuntimeValue&, const RuntimeValue&) {};
}

NativeFunctionFn methodFunction() {
    return [](VirtualMachine*) {};
}

std::unique_ptr<Expression> parseExpression(CompilerContext& context, Function* function, const NomadString& source) {
    Tokenizer tokens(&context, "test.nomad", source);
    auto expression = parser::parseExpression(&context, function, &tokens);

    if (expression && !tokens.endOfLine()) {
        context.reportError("Test expression was not fully parsed", &tokens);
    }

    return expression;
}

} // namespace

BOOST_AUTO_TEST_SUITE(intrinsic_member_syntax)

BOOST_AUTO_TEST_CASE(resolves_property_on_literal) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    auto expression = parseExpression(context, nullptr, "\"Nomad\".length");

    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(expression->resolve(&context, nullptr));
    BOOST_TEST(expression->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getErrorCount() == 0);
}

BOOST_AUTO_TEST_CASE(resolves_property_on_qualified_local) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Function function(NOMAD_INVALID_ID, "test", "test.nomad", "");
    function.registerVariable("name", runtime.getStringType());

    auto expression = parseExpression(context, &function, "name.length");

    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(expression->resolve(&context, &function));
    BOOST_TEST(expression->getType() == runtime.getIntegerType());
}

BOOST_AUTO_TEST_CASE(resolves_method_arguments_using_receiver_type) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Function function(NOMAD_INVALID_ID, "test", "test.nomad", "");
    function.registerVariable("name", runtime.getStringType());

    auto expression = parseExpression(context, &function, "name.contains \"mad\"");

    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(expression->resolve(&context, &function));
    BOOST_TEST(expression->getType() == runtime.getBooleanType());
}

BOOST_AUTO_TEST_CASE(resolves_chained_members_on_function_return) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    const auto functionId = runtime.registerFunction("getName", "getName.nomad", "return \"Nomad\"");
    runtime.getFunction(functionId)->setReturnType(runtime.getStringType());

    auto expression = parseExpression(context, nullptr, "getName.toUpper.length");

    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(expression->resolve(&context, nullptr));
    BOOST_TEST(expression->getType() == runtime.getIntegerType());
}

BOOST_AUTO_TEST_CASE(rejects_reading_write_only_property) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    BOOST_REQUIRE(runtime.registerIntrinsicProperty(runtime.getStringType(), "sink", runtime.getIntegerType(), {},
                                                    propertySetter(), NomadDoc("Sink")) != NOMAD_INVALID_ID);

    auto expression = parseExpression(context, nullptr, "\"Nomad\".sink");

    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(!expression->resolve(&context, nullptr));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->message.find("write-only") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(rejects_writing_read_only_property) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Function function(NOMAD_INVALID_ID, "test", "test.nomad", "");
    function.registerVariable("name", runtime.getStringType());

    Tokenizer tokens(&context, "test.nomad", "name.length = 10");
    auto statement = parser::parseLine(&context, &function, &tokens);

    BOOST_REQUIRE(statement != nullptr);
    BOOST_TEST(!statement->resolve(&context, &function));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->message.find("read-only") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(rejects_mutating_method_on_temporary) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    BOOST_REQUIRE(runtime.registerIntrinsicMethod(runtime.getStringType(), "clear", methodFunction(), {},
                                                  runtime.getVoidType(), true, NomadDoc("Clear")) != NOMAD_INVALID_ID);

    Tokenizer tokens(&context, "test.nomad", "\"Nomad\".clear");
    auto statement = parser::parseLine(&context, nullptr, &tokens);

    BOOST_REQUIRE(statement != nullptr);
    BOOST_TEST(!statement->resolve(&context, nullptr));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->message.find("temporary or read-only receiver") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(preserves_existing_qualified_identifier) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Function function(NOMAD_INVALID_ID, "test", "test.nomad", "");
    function.registerVariable("name", runtime.getStringType());
    function.registerVariable("name.length", runtime.getIntegerType());

    auto expression = parseExpression(context, &function, "name.length");

    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(expression->resolve(&context, &function));
    BOOST_TEST(expression->getType() == runtime.getIntegerType());
}

BOOST_AUTO_TEST_SUITE_END()
