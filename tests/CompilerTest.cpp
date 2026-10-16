// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/script/Runtime.hpp>

using namespace nomad;

BOOST_AUTO_TEST_CASE(script_registration_reports_collisions_immediately)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    BOOST_REQUIRE(compiler->registerScriptFile(&context, "answer", "first.nomad", "return 1") != NOMAD_INVALID_ID);
    BOOST_TEST(compiler->registerScriptFile(&context, "answer", "second.nomad", "return 2") == NOMAD_INVALID_ID);

    BOOST_REQUIRE(context.getErrorCount() == 1U);
    BOOST_TEST(context.getFirstError()->sourceName == "second.nomad");
    BOOST_TEST(context.getFirstError()->message.find("already used") != NomadString::npos);
    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_TEST(context.getErrorCount() == 1U);
}

BOOST_AUTO_TEST_CASE(source_registration_reports_loading_diagnostics_immediately)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const SourceBatch batch{
        {{"broken", {"untitled:broken", "return missing"}}},
        {{DiagnosticSeverity::Warning, "Loading warning", "loader", 1, 2},
         {DiagnosticSeverity::Error, "Loading error", "loader", 3, 4}}
    };

    compiler->registerSources(&context, batch);

    BOOST_TEST(context.getWarningCount() == 1U);
    BOOST_TEST(context.getErrorCount() == 1U);
    BOOST_REQUIRE(context.getDiagnostics().size() == 2U);
    BOOST_TEST(context.getDiagnostics()[0].message == "Loading warning");
    BOOST_TEST(context.getDiagnostics()[1].message == "Loading error");
    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_TEST(context.getDiagnostics().size() == 2U);
}

BOOST_AUTO_TEST_CASE(compilation_does_not_ignore_preexisting_context_errors)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerScriptFile(&context, "broken", "broken.nomad", "return missing");
    context.reportError("Existing error");

    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE(context.getDiagnostics().size() == 1U);
    BOOST_TEST(context.getFirstError()->message == "Existing error");
}

BOOST_AUTO_TEST_CASE(script_registration_and_compilation_require_matching_contexts)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    auto otherCompiler = runtime.createCompiler();
    CompilerContext otherContext(otherCompiler.get());
    const SourceBatch batch;

    BOOST_CHECK_THROW(compiler->registerSources(nullptr, batch), NomadBug);
    BOOST_CHECK_THROW(compiler->registerSources(&otherContext, batch), NomadBug);
    BOOST_CHECK_THROW(compiler->registerScriptFile(nullptr, "test", "test.nomad", ""), NomadBug);
    BOOST_CHECK_THROW(compiler->registerScriptFile(&otherContext, "test", "test.nomad", ""), NomadBug);
    BOOST_CHECK_THROW(compiler->compileFunctions(nullptr), NomadBug);
    BOOST_CHECK_THROW(compiler->compileFunctions(&otherContext), NomadBug);
}

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

BOOST_AUTO_TEST_CASE(registered_source_batches_are_owned_by_the_compiler)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    {
        SourceBatch batch{{{"answer", {"untitled:answer", "return 42"}}}, {}};
        compiler->registerSources(&context, batch);
        batch.sources.front().source.text = "return missing";
    }
    BOOST_REQUIRE(compiler->compileFunctions(&context));

    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("answer"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(context_variables_compile_and_execute_with_global_ids)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const auto functionId = compiler->registerScriptFile(
        &context,
        "contextVariable",
        "contextVariable.nomad",
        "global.value = 17\nreturn global.value"
    );
    BOOST_REQUIRE(compiler->compileFunctions(&context));

    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getIntegerValue() == 17);
}

BOOST_AUTO_TEST_CASE(context_variables_in_format_strings_use_global_ids)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const auto functionId = compiler->registerScriptFile(
        &context,
        "contextFormatString",
        "contextFormatString.nomad",
        "global.value = 17\nreturn $\"value {global.value}\""
    );
    BOOST_REQUIRE(compiler->compileFunctions(&context));

    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getStringValue() == "value 17");

    runtime.getStringType()->freeValue(result);
}
