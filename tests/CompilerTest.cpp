// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptFunction.hpp>

using namespace nomad;

BOOST_AUTO_TEST_CASE(script_registration_reports_collisions_immediately)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerScripts(&context, {{{"answer", {"first.nomad", "return 1"}}}, {}});
    BOOST_REQUIRE(runtime.getFunctionId("answer") != NOMAD_INVALID_ID);
    compiler->registerScripts(&context, {{{"answer", {"second.nomad", "return 2"}}}, {}});

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

    compiler->registerScripts(&context, batch);

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
    compiler->registerScripts(&context, {{{"broken", {"broken.nomad", "return missing"}}}, {}});
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

    BOOST_CHECK_THROW(compiler->registerScripts(nullptr, batch), NomadBug);
    BOOST_CHECK_THROW(compiler->registerScripts(&otherContext, batch), NomadBug);
    BOOST_CHECK_THROW((void)compiler->createFunction(nullptr, {}, "test"), NomadBug);
    BOOST_CHECK_THROW((void)compiler->createFunction(&otherContext, {}, "test"), NomadBug);
    CompilerContext context(compiler.get());
    BOOST_CHECK_THROW((void)compiler->createFunction(&context, {}, "test"), NomadBug);
    BOOST_CHECK_THROW((void)compiler->getScript({}), NomadBug);
    BOOST_CHECK_THROW((void)compiler->getScriptId(NOMAD_INVALID_ID), NomadBug);
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
        compiler->registerScripts(&context, batch);
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
    compiler->registerScripts(&context, {{{"contextVariable", {"contextVariable.nomad",
        "global.value = 17\nreturn global.value"}}}, {}});
    const auto functionId = runtime.getFunctionId("contextVariable");
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
    compiler->registerScripts(&context, {{{"contextFormatString", {"contextFormatString.nomad",
        "global.value = 17\nreturn $\"value {global.value}\""}}}, {}});
    const auto functionId = runtime.getFunctionId("contextFormatString");
    BOOST_REQUIRE(compiler->compileFunctions(&context));

    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);

    BOOST_TEST(result.getStringValue() == "value 17");

    runtime.getStringType()->freeValue(result);
}

BOOST_AUTO_TEST_CASE(functions_keep_their_script_identity_across_registration_and_sorting)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const auto path = std::filesystem::path{"not-on-disk"} / "z.nomad";
    const NomadString text =
        "fun declared value:int\n"
        "    return value + 1\n"
        "end\n"
        "return $\"{declared 41}\"";
    {
        SourceBatch batch{{{"z", {"editor:z", text, path}}}, {}};
        compiler->registerScripts(&context, batch);
        batch.sources.front().source.filePath.reset();
    }
    const auto scriptId = compiler->getScriptId(runtime.getFunctionId("z"));
    compiler->registerScripts(&context, {{{"a", {"untitled:a", "return 1"}}}, {}});
    BOOST_REQUIRE(compiler->compileFunctions(&context));

    const auto& script = compiler->getScript(scriptId);
    BOOST_TEST(script.scriptName == "z");
    BOOST_TEST(script.source.sourceName == "editor:z");
    BOOST_TEST(script.source.text == text);
    BOOST_REQUIRE(script.source.filePath.has_value());
    BOOST_TEST(*script.source.filePath == path);
    BOOST_TEST(compiler->getScriptId(runtime.getFunctionId("declared")).index == scriptId.index);
    BOOST_TEST(compiler->getScriptId(runtime.getFunctionId("a")).index != scriptId.index);
    BOOST_TEST(!compiler->getScript(compiler->getScriptId(runtime.getFunctionId("a"))).source.filePath.has_value());

    std::vector<ScriptFunction*> functions;
    runtime.getFunctions(functions);
    auto generatedCount = NomadIndex{0};
    for (const auto* function : functions) {
        if (function->getParentId() == runtime.getFunctionId("z")) {
            ++generatedCount;
            BOOST_TEST(compiler->getScriptId(function->getId()).index == scriptId.index);
            BOOST_TEST(function->getPath() == "editor:z");
            BOOST_TEST(function->getSource() == text);
        }
    }
    BOOST_TEST(generatedCount == 1U);
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("z"), {}, result);
    BOOST_TEST(result.getStringValue() == "42");
    runtime.getStringType()->freeValue(result);
}

BOOST_AUTO_TEST_CASE(function_creation_reports_collisions_against_its_own_script)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerScripts(&context, {{
        {"first", {"untitled:first", "return 1"}},
        {"second", {"untitled:second", "return 2"}}
    }, {}});
    const auto scriptId = compiler->getScriptId(runtime.getFunctionId("second"));
    const auto functionId = compiler->createFunction(&context, scriptId, "first");

    BOOST_TEST(functionId == NOMAD_INVALID_ID);
    BOOST_REQUIRE(context.getErrorCount() == 1U);
    BOOST_TEST(context.getFirstError()->sourceName == "untitled:second");
    BOOST_TEST(context.getFirstError()->message.find("untitled:first") != NomadString::npos);
    BOOST_TEST(!compiler->compileFunctions(&context));
}
