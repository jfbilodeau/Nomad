// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>
#include <nomad/game/EngineApi.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <filesystem>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(compilation)

BOOST_AUTO_TEST_CASE(checks_a_single_source_file)
{
    TestDirectory directory("nomad_compilation_test_single");
    const auto source = directory.write("answer.nomad", "return 42");

    const auto result = checkPath(source);

    BOOST_TEST(result.succeeded());
    BOOST_TEST(result.errorCount == 0U);
    BOOST_TEST(result.diagnostics.empty());
}

BOOST_AUTO_TEST_CASE(checks_source_files_recursively)
{
    TestDirectory directory("nomad_compilation_test_directory");
    directory.write("init.nomad", "return functions.answer");
    directory.write("functions/answer.nomad", "return 42");

    const auto result = checkPath(directory.getPath());

    BOOST_TEST(result.succeeded());
    BOOST_TEST(result.errorCount == 0U);
}

BOOST_AUTO_TEST_CASE(reports_compiler_diagnostics_without_executing_source)
{
    TestDirectory directory("nomad_compilation_test_diagnostic");
    const auto source = directory.write("broken.nomad", "return missing");

    const auto result = checkPath(source);

    BOOST_TEST(!result.succeeded());
    BOOST_TEST(result.errorCount == 1U);
    BOOST_REQUIRE(!result.diagnostics.empty());
    BOOST_TEST(result.diagnostics.front().sourceName == source.generic_string());
    BOOST_TEST(result.diagnostics.front().message.find("Unknown identifier") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(reports_missing_paths)
{
    TestDirectory directory("nomad_compilation_test_missing");
    const auto missing = directory.getPath() / "missing.nomad";

    const auto result = checkPath(missing);

    BOOST_TEST(!result.succeeded());
    BOOST_TEST(result.errorCount == 1U);
    BOOST_REQUIRE(!result.diagnostics.empty());
    BOOST_TEST(result.diagnostics.front().message == "Path does not exist");
}

BOOST_AUTO_TEST_CASE(dumps_generated_instructions_without_executing_source)
{
    TestDirectory directory("nomad_compilation_test_dump");
    const auto source = directory.write("answer.nomad", "return 42");

    const auto result = dumpInstructions(source);

    BOOST_REQUIRE(result.compilation.succeeded());
    BOOST_TEST(result.instructions.find("function answer") != NomadString::npos);
    BOOST_TEST(result.instructions.find("return") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(dumps_only_the_selected_function)
{
    TestDirectory directory("nomad_compilation_test_dump_selected");
    directory.write("first.nomad", "return 1");
    directory.write("second.nomad", "return 2");

    const auto result = dumpInstructions(directory.getPath(), NomadString{"second"});

    BOOST_REQUIRE(result.compilation.succeeded());
    BOOST_TEST(result.instructions.find("function second") != NomadString::npos);
    BOOST_TEST(result.instructions.find("function first") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(reports_unknown_dump_functions)
{
    TestDirectory directory("nomad_compilation_test_dump_unknown");
    const auto source = directory.write("answer.nomad", "return 42");

    const auto result = dumpInstructions(source, NomadString{"missing"});

    BOOST_TEST(!result.compilation.succeeded());
    BOOST_REQUIRE(!result.compilation.diagnostics.empty());
    BOOST_TEST(result.compilation.diagnostics.back().message == "Unknown function 'missing'");
    BOOST_TEST(result.instructions.empty());
}

BOOST_AUTO_TEST_CASE(generates_documentation_without_executing_source)
{
    TestDirectory directory("nomad_compilation_test_documentation");
    const auto source = directory.write("answer.nomad", "return 42");

    const auto result = generateDocumentationForPath(source);

    BOOST_REQUIRE(result.compilation.succeeded());
    BOOST_TEST(result.documentation.find("# Nomad") != NomadString::npos);
    BOOST_TEST(result.documentation.find("## NativeFunctions") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(engine_api_metadata_enables_headless_compilation)
{
    TestDirectory directory("nomad_compilation_test_engine_api");
    const auto source = directory.write(
        "engine.nomad",
        "window.setTitle \"Nomad\"\nreturn (rgb 1 2 3) + alignment.topLeft"
    );

    const auto languageOnly = checkPath(source);
    BOOST_TEST(!languageOnly.succeeded());

    const auto withEngineApi = checkPath(source, registerEngineApi);
    BOOST_REQUIRE(withEngineApi.succeeded());
}

BOOST_AUTO_TEST_SUITE_END()
