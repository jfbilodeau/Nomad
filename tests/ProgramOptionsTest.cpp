// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/ProgramOptions.hpp>

#include <boost/test/unit_test.hpp>

#include <sstream>

using namespace nomad;

namespace {

struct PackageParameters {
    std::optional<std::string> directory;
    bool force = false;
    bool dryRun = false;
};

struct DocumentationParameters {
    std::optional<std::string> function;
    std::string output;
};

int runArguments(const ProgramOptions& program, std::initializer_list<std::string> arguments, std::ostream& output) {
    const std::vector<std::string> tokens(arguments);
    return program.run(tokens, output);
}

} // namespace

BOOST_AUTO_TEST_SUITE(program_options)

BOOST_AUTO_TEST_CASE(binds_members_and_resets_parameters_between_invocations)
{
    std::ostringstream output;
    ProgramOptions program("nomad", "Project manager");
    PackageParameters captured;
    program.addVerb<PackageParameters>("package", [&captured](PackageParameters& parameters) {
        captured = parameters;
        return 7;
    }, "Package a game")
        .addOptionalPositional("directory", &PackageParameters::directory, "Project directory")
        .addFlag("force", "f", &PackageParameters::force, "Replace output")
        .addFlag("dry-run", "", &PackageParameters::dryRun, "Preview files");

    BOOST_TEST(runArguments(program, {"package", "Game 日本語", "-f", "--dry-run"}, output) == 7);
    BOOST_REQUIRE(captured.directory.has_value());
    BOOST_TEST(*captured.directory == "Game 日本語");
    BOOST_TEST(captured.force);
    BOOST_TEST(captured.dryRun);
    BOOST_TEST(runArguments(program, {"package"}, output) == 7);
    BOOST_TEST(!captured.directory.has_value());
    BOOST_TEST(!captured.force);
    BOOST_TEST(!captured.dryRun);
}

BOOST_AUTO_TEST_CASE(global_actions_stand_alone_and_do_not_leak_into_verbs)
{
    std::ostringstream output;
    ProgramOptions program("nomad", "Project manager");
    auto calls = 0;
    program.addGlobal("version", "v", [&calls] {
        ++calls;
        return EXIT_SUCCESS;
    }, "Show version");
    program.addVerb<EmptyParameters>("version", [&calls](EmptyParameters&) {
        ++calls;
        return EXIT_SUCCESS;
    }, "Show version");
    program.addVerb<EmptyParameters>("run", [](EmptyParameters&) {
        return EXIT_SUCCESS;
    }, "Run project");

    BOOST_TEST(runArguments(program, {"-v"}, output) == EXIT_SUCCESS);
    BOOST_TEST(runArguments(program, {"--version"}, output) == EXIT_SUCCESS);
    BOOST_TEST(runArguments(program, {"version"}, output) == EXIT_SUCCESS);
    BOOST_TEST(calls == 3);
    BOOST_CHECK_THROW(runArguments(program, {"-v", "run"}, output), boost::program_options::error);
    BOOST_CHECK_THROW(runArguments(program, {"run", "-v"}, output), boost::program_options::error);
    BOOST_CHECK_THROW(runArguments(program, {"version", "extra"}, output), boost::program_options::error);
    BOOST_CHECK_THROW(runArguments(program, {"run", "--force"}, output), boost::program_options::error);
    BOOST_CHECK_THROW(runArguments(program, {"missing"}, output), boost::program_options::error);
    BOOST_TEST(calls == 3);
}

BOOST_AUTO_TEST_CASE(help_is_generated_from_registrations_and_bypasses_required_values)
{
    std::ostringstream output;
    ProgramOptions program("nomadc", "Compiler tooling");
    auto calls = 0;
    program.addGlobal("help", "h", [&program, &output] {
        program.printHelp(output);
        return EXIT_SUCCESS;
    }, "General help");
    program.addVerb<DocumentationParameters>("docs", [&calls](DocumentationParameters&) {
        ++calls;
        return EXIT_SUCCESS;
    }, "Generate API documentation")
        .addRequiredOption("output", "o", &DocumentationParameters::output, "Markdown output");

    BOOST_TEST(runArguments(program, {"docs", "--help"}, output) == EXIT_SUCCESS);
    BOOST_TEST(runArguments(program, {"docs", "-h"}, output) == EXIT_SUCCESS);
    BOOST_TEST(calls == 0);
    BOOST_TEST(output.str().find("Generate API documentation") != std::string::npos);
    BOOST_TEST(output.str().find("Markdown output") != std::string::npos);
    BOOST_CHECK_THROW(runArguments(program, {"docs"}, output), boost::program_options::error);
    BOOST_TEST(runArguments(program, {"-h"}, output) == EXIT_SUCCESS);
    BOOST_TEST(output.str().find("Compiler tooling") != std::string::npos);
    BOOST_CHECK_THROW(runArguments(program, {"--help", "docs"}, output), boost::program_options::error);
    BOOST_CHECK_THROW(runArguments(program, {"docs", "--unknown", "-h"}, output), boost::program_options::error);
    BOOST_CHECK_THROW(program.printHelp(output, "missing"), boost::program_options::error);
    BOOST_TEST(runArguments(program, {}, output) == EXIT_FAILURE);
}

BOOST_AUTO_TEST_CASE(binds_required_and_optional_values_and_rejects_invalid_input)
{
    std::ostringstream output;
    ProgramOptions program("nomadc", "Compiler tooling");
    DocumentationParameters captured;
    program.addVerb<DocumentationParameters>("docs", [&captured](DocumentationParameters& parameters) {
        captured = parameters;
        return EXIT_SUCCESS;
    }, "Generate documentation")
        .addOption("function", "", &DocumentationParameters::function, "Function name")
        .addRequiredOption("output", "o", &DocumentationParameters::output, "Output file");

    BOOST_TEST(runArguments(program, {"docs", "-o", "api.md", "--function=start"}, output) == EXIT_SUCCESS);
    BOOST_TEST(captured.output == "api.md");
    BOOST_REQUIRE(captured.function.has_value());
    BOOST_TEST(*captured.function == "start");
    BOOST_CHECK_THROW(runArguments(program, {"docs", "--output"}, output), boost::program_options::error);
    BOOST_CHECK_THROW(runArguments(program, {"docs", "--out", "api.md"}, output), boost::program_options::error);
    BOOST_CHECK_THROW(runArguments(program, {"docs", "-o", "a", "-o", "b"}, output), boost::program_options::error);
}

BOOST_AUTO_TEST_CASE(required_positionals_and_option_terminator)
{
    struct Parameters {
        std::string file;
    };
    std::ostringstream output;
    ProgramOptions program("nomad", "Project manager");
    std::string captured;
    program.addVerb<Parameters>("check", [&captured](Parameters& parameters) {
        captured = parameters.file;
        return EXIT_SUCCESS;
    }, "Check a file")
        .addRequiredPositional("file", &Parameters::file, "Input file");
    BOOST_CHECK_THROW(runArguments(program, {"check"}, output), boost::program_options::error);
    BOOST_TEST(runArguments(program, {"check", "--", "-file"}, output) == EXIT_SUCCESS);
    BOOST_TEST(captured == "-file");
}

BOOST_AUTO_TEST_CASE(rejects_duplicate_and_reserved_registrations)
{
    ProgramOptions program("nomad", "Project manager");
    const auto handler = [](PackageParameters&) { return EXIT_SUCCESS; };
    auto& verb = program.addVerb<PackageParameters>("package", handler, "Package a game");
    verb.addFlag("force", "f", &PackageParameters::force, "Replace output");
    BOOST_CHECK_THROW(verb.addFlag("help", "", &PackageParameters::force, "Help"), std::invalid_argument);
    BOOST_CHECK_THROW(verb.addFlag("force", "", &PackageParameters::force, "Force"), std::invalid_argument);
    BOOST_CHECK_THROW(verb.addFlag("other", "f", &PackageParameters::force, "Other"), std::invalid_argument);
    BOOST_CHECK_THROW(program.addVerb<PackageParameters>("package", handler, "Duplicate"), std::invalid_argument);
    program.addGlobal("version", "v", [] { return 0; }, "Show version");
    BOOST_CHECK_THROW(program.addGlobal("verbose", "v", [] { return 0; }, "Verbose"), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
