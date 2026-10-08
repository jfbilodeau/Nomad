// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>
#include <nomad/game/GameFactory.hpp>
#include <nomad/project/ProjectConfiguration.hpp>

#include <TestDirectory.hpp>

#include <boost/program_options/errors.hpp>
#include <boost/test/unit_test.hpp>

using namespace nomad;
using namespace nomad::test;

namespace {

constexpr auto PROJECT_CONFIGURATION = R"(schema = 1

[project]
name = "Runtime Test"
identifier = "com.example.runtime-test"
version = "0.1.0"
executable = "runtime-test"
entry = "start"

[nomad]
version = "0.1.0"

[resources]
directory = "assets"

[package]
output = "dist"
exclude = []
)";

} // namespace

BOOST_AUTO_TEST_SUITE(game_factory)

BOOST_AUTO_TEST_CASE(runtime_options_accept_debug_and_resource_path)
{
    char program[] = "nomad-runtime";
    char resourcePathOption[] = "--resource-path";
    char resourcePath[] = "project-resources";
    char debugOption[] = "--debug";
    char* arguments[] = { program, resourcePathOption, resourcePath, debugOption };
    GameOptions options;

    parseCommandLine(4, arguments, &options);

    BOOST_TEST(options.resourcePath == "project-resources");
    BOOST_TEST(options.debug);
}

BOOST_AUTO_TEST_CASE(runtime_options_load_project_resources_and_entry_function)
{
    TestDirectory directory("nomad_game_factory_project");
    directory.write("nomad.toml", PROJECT_CONFIGURATION);
    directory.write("assets/scripts/start.nomad", "return 0");
    GameOptions options;

    loadProjectOptions(directory.getPath() / "assets/scripts", &options);

    BOOST_TEST(options.resourcePath == (directory.getPath() / "assets").lexically_normal().string());
    BOOST_TEST(options.entryFunction == "start");
}

BOOST_AUTO_TEST_CASE(runtime_options_load_release_manifest_defaults)
{
    TestDirectory directory("nomad_game_factory_release_project");
    directory.write(
        "nomad.toml",
        R"(schema = 1

[project]
name = "Runtime Test"
identifier = "com.example.runtime-test"
version = "0.1.0"
entry = "start"

[nomad]
version = "0.1.0"
)"
    );
    directory.write("res/scripts/start.nomad", "return\n");
    GameOptions options;

    loadProjectOptions(directory.getPath(), &options);

    BOOST_TEST(options.resourcePath == (directory.getPath() / "res").lexically_normal().string());
    BOOST_TEST(options.entryFunction == "start");
}

BOOST_AUTO_TEST_CASE(runtime_options_reject_newer_nomad_versions)
{
    TestDirectory directory("nomad_game_factory_newer_version");
    auto configuration = NomadString(PROJECT_CONFIGURATION);
    const auto version = configuration.find("version = \"0.1.0\"", configuration.find("[nomad]"));
    configuration.replace(version, 17U, "version = \"999.0.0\"");
    directory.write("nomad.toml", configuration);
    directory.write("assets/scripts/start.nomad", "return\n");
    GameOptions options;

    BOOST_CHECK_EXCEPTION(
        loadProjectOptions(directory.getPath(), &options),
        ProjectConfigurationError,
        [](const ProjectConfigurationError& error) {
            return NomadString(error.what()).find("requires Nomad 999.0.0") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(runtime_resource_path_override_takes_precedence)
{
    TestDirectory directory("nomad_game_factory_override");
    directory.write("nomad.toml", PROJECT_CONFIGURATION);
    directory.write("assets/scripts/start.nomad", "return 0");
    const auto overridePath = directory.write("override/scripts/start.nomad", "return 0").parent_path().parent_path();
    GameOptions options;
    options.resourcePath = overridePath.string();

    loadProjectOptions(directory.getPath(), &options);

    BOOST_TEST(options.resourcePath == overridePath.string());
    BOOST_TEST(options.entryFunction == "start");
}

BOOST_AUTO_TEST_CASE(runtime_options_report_missing_resources)
{
    TestDirectory directory("nomad_game_factory_missing_resources");
    directory.write("nomad.toml", PROJECT_CONFIGURATION);
    GameOptions options;

    BOOST_CHECK_EXCEPTION(
        loadProjectOptions(directory.getPath(), &options),
        ProjectConfigurationError,
        [](const ProjectConfigurationError& error) {
            return NomadString(error.what()).find("Resource directory does not exist") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(runtime_options_reject_removed_generation_flags)
{
    char program[] = "nomad-runtime";
    char documentationOption[] = "--doc";
    char* documentationArguments[] = { program, documentationOption };
    GameOptions documentationOptions;

    BOOST_CHECK_THROW(
        parseCommandLine(2, documentationArguments, &documentationOptions),
        boost::program_options::unknown_option
    );

    char grammarOption[] = "--tm";
    char* grammarArguments[] = { program, grammarOption };
    GameOptions grammarOptions;

    BOOST_CHECK_THROW(
        parseCommandLine(2, grammarArguments, &grammarOptions),
        boost::program_options::unknown_option
    );
}

BOOST_AUTO_TEST_SUITE_END()
