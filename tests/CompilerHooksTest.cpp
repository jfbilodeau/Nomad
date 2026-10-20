// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/CompilerTools.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>

#include <boost/test/unit_test.hpp>

#include <utility>
#include <vector>

using namespace nomad;

BOOST_AUTO_TEST_SUITE(compiler_hooks)

BOOST_AUTO_TEST_CASE(executable_hooks_follow_phase_and_registration_order)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const auto instructionCount = runtime.getInstructions().size();
    std::vector<std::pair<CompilationPhase, int>> calls;
    compiler->registerPhaseHook([&](CompilerContext* hookContext, const CompilationPhase phase) {
        BOOST_TEST(hookContext == &context);
        calls.emplace_back(phase, 1);
        if (phase == CompilationPhase::Configure) {
            compiler->registerSources(hookContext, {{{"answer", {"editor:answer", "return 42"}}}, {}});
        }
        if (phase == CompilationPhase::Emit) {
            const auto* function = runtime.getScriptFunction(runtime.getFunctionId("answer"));
            BOOST_REQUIRE(function != nullptr);
            BOOST_TEST(function->getReturnType() == runtime.getIntegerType());
            BOOST_TEST(runtime.getInstructions().size() == instructionCount);
        }
    });
    compiler->registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
        calls.emplace_back(phase, 2);
    });

    BOOST_REQUIRE(compiler->compileScripts(&context));

    const std::vector<CompilationPhase> phases{
        CompilationPhase::Configure,
        CompilationPhase::PreParse,
        CompilationPhase::Parse,
        CompilationPhase::PrepareFormatStrings,
        CompilationPhase::Resolve,
        CompilationPhase::Emit,
        CompilationPhase::Link,
        CompilationPhase::Verify,
        CompilationPhase::Completed
    };
    std::vector<std::pair<CompilationPhase, int>> expected;
    for (const auto phase : phases) {
        expected.emplace_back(phase, 1);
        expected.emplace_back(phase, 2);
    }
    BOOST_CHECK(calls == expected);
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("answer"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(analysis_hooks_skip_emission_linking_and_verification)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const auto instructionCount = runtime.getInstructions().size();
    compiler->registerSources(&context, {{{"main", {"editor:main",
        "fun increment value:int\n"
        "result = value + 1\n"
        "return result\n"
        "end\n"
        "global.value = increment 41\n"
        "return global.value"}}}, {}});
    std::vector<CompilationPhase> phases;
    compiler->registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
        phases.push_back(phase);
    });

    BOOST_REQUIRE(compiler->compileScripts(&context, CompilationTarget::Analysis));

    const std::vector<CompilationPhase> expected{
        CompilationPhase::Configure,
        CompilationPhase::PreParse,
        CompilationPhase::Parse,
        CompilationPhase::PrepareFormatStrings,
        CompilationPhase::Resolve,
        CompilationPhase::Completed
    };
    BOOST_CHECK(phases == expected);
    BOOST_TEST(!context.hasError());
    BOOST_TEST(runtime.getInstructions().size() == instructionCount);
    const auto* main = runtime.getScriptFunction(runtime.getFunctionId("main"));
    const auto* increment = runtime.getScriptFunction(runtime.getFunctionId("increment"));
    BOOST_REQUIRE(main != nullptr);
    BOOST_REQUIRE(increment != nullptr);
    BOOST_TEST(main->getReturnType() == runtime.getIntegerType());
    BOOST_TEST(increment->getReturnType() == runtime.getIntegerType());
    BOOST_TEST(increment->getParameterType(increment->getParameterId("value")) == runtime.getIntegerType());
    BOOST_TEST(increment->getVariableType(increment->getVariableId("result")) == runtime.getIntegerType());
    BOOST_TEST(main->getFunctionStart() == NOMAD_INVALID_INDEX);
    BOOST_TEST(increment->getFunctionStart() == NOMAD_INVALID_INDEX);
}

BOOST_AUTO_TEST_CASE(hook_errors_stop_remaining_hooks_and_phases)
{
    const std::vector<CompilationPhase> phases{
        CompilationPhase::Configure,
        CompilationPhase::PreParse,
        CompilationPhase::Parse,
        CompilationPhase::PrepareFormatStrings,
        CompilationPhase::Resolve,
        CompilationPhase::Emit,
        CompilationPhase::Link,
        CompilationPhase::Verify,
        CompilationPhase::Completed
    };
    for (auto failureIndex = std::size_t{0}; failureIndex < phases.size(); ++failureIndex) {
        Runtime runtime;
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{{"answer", {"editor:answer", "return 42"}}}, {}});
        std::vector<CompilationPhase> firstCalls;
        std::vector<CompilationPhase> secondCalls;
        compiler->registerPhaseHook([&](CompilerContext* hookContext, const CompilationPhase phase) {
            firstCalls.push_back(phase);
            if (phase == phases[failureIndex]) {
                hookContext->reportError("Hook failed");
            }
        });
        compiler->registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
            secondCalls.push_back(phase);
        });

        BOOST_TEST(!compiler->compileScripts(&context));
        BOOST_REQUIRE(context.getErrorCount() == 1U);
        BOOST_TEST(context.getFirstError()->message == "Hook failed");
        BOOST_REQUIRE(firstCalls.size() == failureIndex + 1);
        BOOST_REQUIRE(secondCalls.size() == failureIndex);
        for (auto index = std::size_t{0}; index < firstCalls.size(); ++index) {
            BOOST_CHECK(firstCalls[index] == phases[index]);
        }
        for (auto index = std::size_t{0}; index < secondCalls.size(); ++index) {
            BOOST_CHECK(secondCalls[index] == phases[index]);
        }
    }
}

BOOST_AUTO_TEST_CASE(hook_exceptions_become_diagnostics)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    auto remainingHookCalled = false;
    compiler->registerPhaseHook([](CompilerContext*, CompilationPhase) {
        throw NomadException("Module configuration failed");
    });
    compiler->registerPhaseHook([&](CompilerContext*, CompilationPhase) {
        remainingHookCalled = true;
    });

    BOOST_TEST(!compiler->compileScripts(&context));
    BOOST_REQUIRE(context.getErrorCount() == 1U);
    BOOST_TEST(context.getFirstError()->message == "Module configuration failed");
    BOOST_TEST(!remainingHookCalled);
}

BOOST_AUTO_TEST_CASE(input_errors_prevent_configuration_hooks)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    auto hookCalled = false;
    compiler->registerPhaseHook([&](CompilerContext*, CompilationPhase) {
        hookCalled = true;
    });
    compiler->registerSources(&context, {
        {{"answer", {"editor:answer", "return 42"}}},
        {{DiagnosticSeverity::Error, "Loading failed", "loader", 1, 1}}
    });

    BOOST_TEST(!compiler->compileScripts(&context));
    BOOST_TEST(!hookCalled);
    BOOST_TEST(context.getErrorCount() == 1U);
}

BOOST_AUTO_TEST_CASE(semantic_errors_prevent_emission_and_completion_hooks)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"broken", {"editor:broken", "return missing"}}}, {}});
    const auto instructionCount = runtime.getInstructions().size();
    std::vector<CompilationPhase> phases;
    compiler->registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
        phases.push_back(phase);
    });

    BOOST_TEST(!compiler->compileScripts(&context));
    const std::vector<CompilationPhase> expected{
        CompilationPhase::Configure,
        CompilationPhase::PreParse,
        CompilationPhase::Parse,
        CompilationPhase::PrepareFormatStrings,
        CompilationPhase::Resolve
    };
    BOOST_CHECK(phases == expected);
    BOOST_TEST(runtime.getInstructions().size() == instructionCount);
}

BOOST_AUTO_TEST_CASE(absent_optional_hooks_are_no_ops_and_compatibility_compiles)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerPhaseHook({});
    compiler->registerSources(&context, {{{"answer", {"editor:answer", "return 42"}}}, {}});

    BOOST_REQUIRE(compiler->compileFunctions(&context));
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("answer"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(internal_hook_bugs_propagate_and_allow_a_fresh_attempt)
{
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"answer", {"editor:answer", "return 42"}}}, {}});
    auto fail = true;
    compiler->registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
        if (fail && phase == CompilationPhase::Parse) {
            fail = false;
            throw NomadBug("Hook bug");
        }
    });

    BOOST_CHECK_THROW((void)compiler->compileScripts(&context), NomadBug);
    BOOST_TEST(!context.hasError());
    BOOST_REQUIRE(compiler->compileScripts(&context));
}

BOOST_AUTO_TEST_CASE(analysis_tool_resolves_types_without_function_instructions)
{
    Runtime runtime;
    const SourceBatch batch{{
        {"answer", {"editor:answer", "return 42"}},
        {"formatted", {"editor:formatted", "return $\"answer {answer}\""}}
    }, {}};

    const auto result = analyzeSources(batch, &runtime);

    BOOST_REQUIRE(result.succeeded());
    BOOST_TEST(runtime.getInstructions().size() == 1U);
    const auto* answer = runtime.getScriptFunction(runtime.getFunctionId("answer"));
    const auto* formatted = runtime.getScriptFunction(runtime.getFunctionId("formatted"));
    BOOST_REQUIRE(answer != nullptr);
    BOOST_REQUIRE(formatted != nullptr);
    BOOST_TEST(answer->getReturnType() == runtime.getIntegerType());
    BOOST_TEST(formatted->getReturnType() == runtime.getStringType());
    BOOST_TEST(answer->getFunctionStart() == NOMAD_INVALID_INDEX);
    BOOST_TEST(formatted->getFunctionStart() == NOMAD_INVALID_INDEX);
    BOOST_TEST(analyzeSources(batch).succeeded());
}

BOOST_AUTO_TEST_SUITE_END()
