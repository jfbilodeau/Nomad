// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/Source.hpp>

#include <nomad/script/InstructionDump.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

using namespace nomad;

BOOST_AUTO_TEST_SUITE(compilation)

BOOST_AUTO_TEST_CASE(stream_and_string_diagnostics_preserve_locations_and_append_without_newlines) {
    const std::pair<CompilerDiagnostic, NomadString> cases[]{
        {{CompilerDiagnosticSeverity::Error, "Failed", "main.nomad", 16, 32}, "main.nomad:16:32: error: Failed"},
        {{CompilerDiagnosticSeverity::Warning, "Unused", "main.nomad", 4, NOMAD_INVALID_INDEX}, "main.nomad:4: warning: Unused"},
        {{CompilerDiagnosticSeverity::Error, "Failed", "main.nomad", NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX},
            "main.nomad: error: Failed"},
        {{CompilerDiagnosticSeverity::Error, "Failed", "", NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX}, "error: Failed"},
        {{CompilerDiagnosticSeverity::Warning, "Unused", "", 4, 2}, ":4:2: warning: Unused"},
        {{CompilerDiagnosticSeverity::Warning, "Unused", "", NOMAD_INVALID_INDEX, 2}, "warning: Unused"}
    };
    for (const auto& [diagnostic, expected] : cases) {
        std::ostringstream stream;
        stream << "prefix " << std::hex;
        formatDiagnostic(diagnostic, stream);
        stream << " suffix";
        BOOST_TEST(stream.str() == "prefix " + expected + " suffix");
        BOOST_TEST(formatDiagnostic(diagnostic) == expected);
    }
}

BOOST_AUTO_TEST_CASE(instruction_dump_formats_existing_code_without_compiling_or_executing) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    auto calls = 0;
    runtime.registerNativeFunction("probe", [&](VirtualMachine*) { ++calls; }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(compileSources({{{"main", {"editor:main",
        "fun extra\nreturn 1\nend\nprobe\nreturn 42"}}}, {}}, *compiler).succeeded());
    const auto instructionCount = runtime.getInstructions().size();
    const auto* function = runtime.getScriptFunction(runtime.getFunctionId("main"));
    BOOST_REQUIRE(function != nullptr);

    std::ostringstream all;
    dumpInstructions(runtime, all);
    std::ostringstream selected;
    dumpInstructions(runtime, *function, selected);

    BOOST_TEST(selected.str().starts_with("function main\n"));
    BOOST_TEST(selected.str().find("function extra") == NomadString::npos);
    BOOST_TEST(all.str().find("function extra\n") != NomadString::npos);
    BOOST_TEST(all.str().find(selected.str()) != NomadString::npos);
    BOOST_TEST(selected.str().find("probe (") != NomadString::npos);
    BOOST_TEST(selected.str().find(": 42\n") != NomadString::npos);
    BOOST_TEST(calls == 0);
    BOOST_TEST(runtime.getInstructions().size() == instructionCount);
}

BOOST_AUTO_TEST_CASE(instruction_dump_skips_analysis_only_callables) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    BOOST_REQUIRE(compileSources({{{"main", {"editor:main", "return 42"}}}, {}},
        *compiler, CompilationTarget::Analysis).succeeded());
    std::ostringstream stream;
    dumpInstructions(runtime, stream);
    BOOST_TEST(stream.str().empty());
}

BOOST_AUTO_TEST_CASE(instruction_dump_keeps_struct_signatures_logical_without_executing) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    auto calls = 0;
    runtime.registerNativeFunction("probe", [&](VirtualMachine*) { ++calls; }, {}, runtime.getVoidType(), "");
    const auto result = compileSources({{{"main", {"editor:structs",
        "struct DumpEmpty\nend\n"
        "struct DumpLeaf\ntext = \"owned\"\n"
        "init number:int\nprobe\nthis.number = number\nend\n"
        "fun read value:DumpLeaf empty:DumpEmpty\nreturn value.number\nend\nend\n"
        "fun makeDump value:DumpLeaf empty:DumpEmpty\ncandidate = new DumpLeaf 17\nreturn candidate\nend\n"
        "probe\nassert false\nreturn 0"}}}, {}}, *compiler);
    BOOST_REQUIRE_MESSAGE(result.succeeded(),
        (result.diagnostics.empty() ? "" : result.diagnostics.front().message));
    const auto instructionCount = runtime.getInstructions().size();
    const auto* function = runtime.getScriptFunction(runtime.getFunctionId("makeDump"));
    BOOST_REQUIRE(function != nullptr);
    BOOST_TEST(function->getVariableStorageSize() == 0U);
    BOOST_TEST(function->getParameterCount() == 2U);
    BOOST_TEST(function->getParameterStorageSize() == 2U);
    std::ostringstream selected;
    dumpInstructions(runtime, *function, selected);
    BOOST_TEST(selected.str().starts_with("function makeDump value:DumpLeaf empty:DumpEmpty\n"));
    BOOST_TEST(selected.str().find("Unknown instruction") == NomadString::npos);
    std::ostringstream all;
    dumpInstructions(runtime, all);
    BOOST_TEST(all.str().find("method DumpLeaf.read value:DumpLeaf empty:DumpEmpty\n") != NomadString::npos);
    BOOST_TEST(all.str().find("constructor DumpLeaf number:int\n") != NomadString::npos);
    BOOST_TEST(all.str().find("constructor DumpEmpty\n") != NomadString::npos);
    BOOST_TEST(calls == 0);
    BOOST_TEST(runtime.getInstructions().size() == instructionCount);
}

BOOST_AUTO_TEST_CASE(configured_compilation_preserves_targets_hooks_diagnostics_and_execution_boundaries) {
    for (const auto target : {CompilationTarget::Analysis, CompilationTarget::Executable}) {
        Runtime runtime;
        auto virtualMachine = runtime.createVirtualMachine();
        auto compiler = runtime.createCompiler();
        auto calls = 0;
        runtime.registerNativeFunction("probe", [&](VirtualMachine*) { ++calls; }, {}, runtime.getVoidType(), "");
        auto configured = false;
        compiler->registerPhaseHook([&](CompilerContext* context, const CompilationPhase phase) {
            BOOST_TEST(context->getCompiler() == compiler.get());
            if (phase == CompilationPhase::Configure) {
                configured = true;
                context->reportWarning("Hook warning", "editor:main", 1, 1);
            }
        });
        const auto instructionCount = runtime.getInstructions().size();
        const SourceBatch sources{
            {{"main", {"editor:main", "probe\nreturn 42"}}},
            {{CompilerDiagnosticSeverity::Warning, "Loading warning", "editor:main", 1, 1}}
        };

        const auto result = compileSources(sources, *compiler, target);

        BOOST_REQUIRE(result.succeeded());
        BOOST_TEST(configured);
        BOOST_TEST(result.errorCount == 0U);
        BOOST_TEST(result.warningCount == 2U);
        BOOST_REQUIRE(result.diagnostics.size() == 2U);
        BOOST_TEST(result.diagnostics[0].message == "Loading warning");
        BOOST_TEST(result.diagnostics[1].message == "Hook warning");
        BOOST_TEST(calls == 0);
        const auto* function = runtime.getScriptFunction(runtime.getFunctionId("main"));
        BOOST_REQUIRE(function != nullptr);
        BOOST_TEST(function->getReturnType() == runtime.getIntegerType());
        if (target == CompilationTarget::Analysis) {
            BOOST_TEST(runtime.getInstructions().size() == instructionCount);
            BOOST_TEST(function->getFunctionStart() == NOMAD_INVALID_INDEX);
        } else {
            RuntimeValue value;
            virtualMachine->executeFunction(function->getId(), {}, value);
            BOOST_TEST(value.getIntegerValue() == 42);
            BOOST_TEST(calls == 1);
        }
    }
}

BOOST_AUTO_TEST_CASE(compilation_results_own_loading_diagnostics_and_preserve_counts_and_locations) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    SourceBatch sources{
        {},
        {
            {CompilerDiagnosticSeverity::Warning, "Loading warning", "warning.nomad", 2, 3},
            {CompilerDiagnosticSeverity::Error, "Loading error", "error.nomad", 4, 5}
        }
    };

    const auto result = compileSources(sources, *compiler);
    sources.diagnostics.clear();

    BOOST_TEST(!result.succeeded());
    BOOST_TEST(result.errorCount == 1U);
    BOOST_TEST(result.warningCount == 1U);
    BOOST_REQUIRE(result.diagnostics.size() == 2U);
    BOOST_TEST(formatDiagnostic(result.diagnostics[0]) == "warning.nomad:2:3: warning: Loading warning");
    BOOST_TEST(formatDiagnostic(result.diagnostics[1]) == "error.nomad:4:5: error: Loading error");
}

BOOST_AUTO_TEST_CASE(configured_compilation_returns_script_diagnostics_for_both_targets) {
    for (const auto target : {CompilationTarget::Analysis, CompilationTarget::Executable}) {
        Runtime runtime;
        auto compiler = runtime.createCompiler();
        const auto result = compileSources(
            {{{"invalid", {"editor:invalid", "return unknownIdentifier"}}}, {}}, *compiler, target
        );
        BOOST_TEST(!result.succeeded());
        BOOST_TEST(result.errorCount > 0U);
        BOOST_TEST(std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
            return diagnostic.severity == CompilerDiagnosticSeverity::Error &&
                diagnostic.sourceName == "editor:invalid" &&
                diagnostic.message.find("unknownIdentifier") != NomadString::npos;
        }));
    }
}

BOOST_AUTO_TEST_SUITE_END()
