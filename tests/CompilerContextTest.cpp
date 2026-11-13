// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Runtime.hpp>

using namespace nomad;

BOOST_AUTO_TEST_CASE(compiler_context_imports_owned_diagnostics_with_counts_and_locations)
{
    Runtime runtime;
    const auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    std::vector<CompilerDiagnostic> diagnostics{
        {CompilerDiagnosticSeverity::Warning, "Loading warning", "first.nomad", 2, 3},
        {CompilerDiagnosticSeverity::Error, "Loading error", "second.nomad", 4, 5,
            CompilerDiagnosticCode::UnknownIdentifier, "missing"}
    };
    context.addDiagnostics(diagnostics);
    diagnostics.clear();

    BOOST_TEST(context.getWarningCount() == 1U);
    BOOST_TEST(context.getErrorCount() == 1U);
    BOOST_REQUIRE(context.getDiagnostics().size() == 2U);
    BOOST_TEST(context.getDiagnostics()[0].message == "Loading warning");
    BOOST_TEST(context.getDiagnostics()[0].sourceName == "first.nomad");
    BOOST_TEST(context.getDiagnostics()[0].line == 2U);
    BOOST_TEST(context.getDiagnostics()[0].column == 3U);
    BOOST_TEST(context.getFirstError()->message == "Loading error");
    BOOST_TEST(context.getFirstError()->sourceName == "second.nomad");
    BOOST_TEST(context.getFirstError()->line == 4U);
    BOOST_TEST(context.getFirstError()->column == 5U);
    BOOST_CHECK(context.getFirstError()->code == CompilerDiagnosticCode::UnknownIdentifier);
    BOOST_TEST(context.getFirstError()->subject == "missing");
    context.clearDiagnostics();
    BOOST_TEST(!context.hasError());
    BOOST_TEST(!context.hasWarning());
}

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

    BOOST_TEST((diagnostics[0].severity == CompilerDiagnosticSeverity::Warning));
    BOOST_TEST(diagnostics[0].message == "Unused variable 'bonus'");
    BOOST_TEST(diagnostics[0].sourceName == "test.nomad");
    BOOST_TEST(diagnostics[0].line == 2);
    BOOST_TEST(diagnostics[0].column == 5);

    BOOST_TEST((diagnostics[1].severity == CompilerDiagnosticSeverity::Error));
    BOOST_CHECK(diagnostics[1].code == CompilerDiagnosticCode::General);
    BOOST_TEST(diagnostics[1].subject.empty());
    BOOST_TEST(diagnostics[1].message == "Unknown identifier: missing");
    BOOST_TEST(diagnostics[1].sourceName == "other.nomad");
    BOOST_TEST(diagnostics[1].line == 3);
    BOOST_TEST(diagnostics[1].column == 1);

    BOOST_TEST((diagnostics[2].severity == CompilerDiagnosticSeverity::Error));
    BOOST_TEST(diagnostics[2].line == NOMAD_INVALID_INDEX);
    BOOST_TEST(diagnostics[2].column == NOMAD_INVALID_INDEX);
}

BOOST_AUTO_TEST_CASE(unknown_identifier_resolution_reports_structured_metadata_without_changing_text)
{
    Runtime runtime;
    const auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(&context, "unknown.nomad", "missing", 7);
    BOOST_REQUIRE(tokens.nextLine());
    const auto expression = parser::parseExpression(&context, nullptr, &tokens);
    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(!expression->resolve(&context, nullptr));
    BOOST_REQUIRE_EQUAL(context.getErrorCount(), 1U);
    const auto* error = context.getFirstError();
    BOOST_REQUIRE(error != nullptr);
    BOOST_CHECK(error->code == CompilerDiagnosticCode::UnknownIdentifier);
    BOOST_TEST(error->subject == "missing");
    BOOST_TEST(error->message == "Unknown identifier: missing");
    BOOST_TEST(error->sourceName.empty());
    BOOST_TEST(error->line == 7U);

    CompilerContext copied(compiler.get());
    auto diagnostic = *error;
    diagnostic.message = "An independently worded error";
    copied.addDiagnostic(diagnostic);
    diagnostic.subject = "changed";
    BOOST_TEST(copied.getErrorCount() == 1U);
    BOOST_TEST(copied.getWarningCount() == 0U);
    BOOST_REQUIRE(copied.getFirstError() != nullptr);
    BOOST_CHECK(copied.getFirstError()->code == CompilerDiagnosticCode::UnknownIdentifier);
    BOOST_TEST(copied.getFirstError()->subject == "missing");
    BOOST_TEST(copied.getFirstError()->message == "An independently worded error");
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
    const CompilerDiagnostic located{CompilerDiagnosticSeverity::Error, "Unknown identifier: missing", "scripts/main.nomad", 3, 7};
    const CompilerDiagnostic lineOnly{CompilerDiagnosticSeverity::Warning, "Unused", "scripts/main.nomad", 4, NOMAD_INVALID_INDEX};
    const CompilerDiagnostic unlocated{CompilerDiagnosticSeverity::Error, "Failed", "", NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX};

    BOOST_TEST(formatDiagnostic(located) == "scripts/main.nomad:3:7: error: Unknown identifier: missing");
    BOOST_TEST(formatDiagnostic(lineOnly) == "scripts/main.nomad:4: warning: Unused");
    BOOST_TEST(formatDiagnostic(unlocated) == "error: Failed");
}
