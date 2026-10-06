// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Runtime.hpp>

using namespace nomad;

BOOST_AUTO_TEST_CASE(compiler_context_starts_without_diagnostics)
{
    Runtime runtime;
    const auto compiler = runtime.createCompiler();
    const CompilerContext context(compiler.get());

    BOOST_TEST(context.getCompiler() == compiler.get());
    BOOST_TEST(context.getRuntime() == &runtime);
    BOOST_TEST(!context.hasError());
    BOOST_TEST(!context.hasWarning());
    BOOST_TEST(context.getDiagnostics().empty());
}

BOOST_AUTO_TEST_CASE(compiler_context_records_errors_and_warnings_in_order)
{
    Runtime runtime;
    const auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    context.reportWarning("Unused variable 'bonus'", "test.nomad", 2, 5);
    context.reportError("Unknown identifier: missing", "other.nomad", 3, 1);
    context.reportError("Missing location");

    BOOST_TEST(context.hasError());
    BOOST_TEST(context.hasWarning());
    BOOST_TEST(context.getErrorCount() == 2);
    BOOST_TEST(context.getWarningCount() == 1);

    const auto diagnostics = context.getDiagnostics();
    BOOST_REQUIRE(diagnostics.size() == 3);

    BOOST_TEST((diagnostics[0].severity == DiagnosticSeverity::Warning));
    BOOST_TEST(diagnostics[0].message == "Unused variable 'bonus'");
    BOOST_TEST(diagnostics[0].sourceName == "test.nomad");
    BOOST_TEST(diagnostics[0].line == 2);
    BOOST_TEST(diagnostics[0].column == 5);

    BOOST_TEST((diagnostics[1].severity == DiagnosticSeverity::Error));
    BOOST_TEST(diagnostics[1].message == "Unknown identifier: missing");
    BOOST_TEST(diagnostics[1].sourceName == "other.nomad");
    BOOST_TEST(diagnostics[1].line == 3);
    BOOST_TEST(diagnostics[1].column == 1);

    BOOST_TEST((diagnostics[2].severity == DiagnosticSeverity::Error));
    BOOST_TEST(diagnostics[2].line == NOMAD_INVALID_INDEX);
    BOOST_TEST(diagnostics[2].column == NOMAD_INVALID_INDEX);
}

BOOST_AUTO_TEST_CASE(compiler_context_warnings_do_not_count_as_errors)
{
    Runtime runtime;
    const auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    context.reportWarning("Just a warning");

    BOOST_TEST(context.hasWarning());
    BOOST_TEST(!context.hasError());
    BOOST_TEST(context.getDiagnostics()[0].sourceName.empty());
}

BOOST_AUTO_TEST_CASE(compiler_context_copies_string_view_diagnostics)
{
    Runtime runtime;
    const auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    NomadString messageBacking = "xxview messageyy";
    NomadString sourceBacking = "xxsource.nomadyy";

    context.reportError(
        NomadStringView(messageBacking.data() + 2, 12),
        NomadStringView(sourceBacking.data() + 2, 12),
        4,
        2
    );
    messageBacking.assign(messageBacking.size(), 'x');
    sourceBacking.assign(sourceBacking.size(), 'x');

    const auto diagnostics = context.getDiagnostics();
    BOOST_REQUIRE(diagnostics.size() == 1);
    BOOST_TEST(diagnostics[0].message == "view message");
    BOOST_TEST(diagnostics[0].sourceName == "source.nomad");
}

BOOST_AUTO_TEST_CASE(compiler_context_clears_diagnostics)
{
    Runtime runtime;
    const auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    context.reportWarning("Warning");
    context.reportError("Error");
    context.clearDiagnostics();

    BOOST_TEST(!context.hasError());
    BOOST_TEST(!context.hasWarning());
    BOOST_TEST(context.getErrorCount() == 0);
    BOOST_TEST(context.getWarningCount() == 0);
    BOOST_TEST(context.getDiagnostics().empty());
}

BOOST_AUTO_TEST_CASE(compiler_context_requires_compiler)
{
    BOOST_CHECK_THROW(CompilerContext(nullptr), NomadBug);
}

BOOST_AUTO_TEST_CASE(compiler_context_formats_diagnostics)
{
    const Diagnostic located{DiagnosticSeverity::Error, "Unknown identifier: missing", "scripts/main.nomad", 3, 7};
    const Diagnostic lineOnly{DiagnosticSeverity::Warning, "Unused", "scripts/main.nomad", 4, NOMAD_INVALID_INDEX};
    const Diagnostic unlocated{DiagnosticSeverity::Error, "Failed", "", NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX};

    BOOST_TEST(formatDiagnostic(located) == "scripts/main.nomad:3:7: error: Unknown identifier: missing");
    BOOST_TEST(formatDiagnostic(lineOnly) == "scripts/main.nomad:4: warning: Unused");
    BOOST_TEST(formatDiagnostic(unlocated) == "error: Failed");
}
