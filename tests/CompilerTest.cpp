// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/script/Runtime.hpp>

using namespace nomad;

BOOST_AUTO_TEST_CASE(statement_parsers_register_without_interpreter_metadata)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();

    compiler->registerParseStatementFn(
        "console.test",
        nullptr,
        nullptr
    );

    BOOST_TEST(compiler->isStatement("console.test"));
}

BOOST_AUTO_TEST_CASE(context_variables_compile_and_execute_with_global_ids)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto functionId = compiler->registerScriptFile(
        "contextVariable",
        "contextVariable.nomad",
        "global.value = 17\nreturn global.value"
    );
    CompilerContext context(compiler.get());

    BOOST_REQUIRE(compiler->compileFunctions(&context));

    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 17);
}

BOOST_AUTO_TEST_CASE(context_variables_in_format_strings_use_global_ids)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    const auto functionId = compiler->registerScriptFile(
        "contextFormatString",
        "contextFormatString.nomad",
        "global.value = 17\nreturn $\"value {global.value}\""
    );
    CompilerContext context(compiler.get());

    BOOST_REQUIRE(compiler->compileFunctions(&context));

    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getStringValue() == "value 17");

    runtime.getStringType()->freeValue(result);
}
