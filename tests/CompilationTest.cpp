// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>

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

BOOST_AUTO_TEST_SUITE_END()
