// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectConfiguration.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <string>

using namespace nomad;
using namespace nomad::test;

namespace {

constexpr auto COMPLETE_CONFIGURATION = R"(schema = 1

[project]
name = "Test Game"
identifier = "com.example.test-game"
version = "0.1.0"
executable = "test-game"
entry = "start"

[nomad]
version = "0.1.0"

[resources]
directory = "res"

[package]
output = "dist"
exclude = ["**/*.psd", "development/**"]
)";

} // namespace

BOOST_AUTO_TEST_SUITE(project_configuration)

BOOST_AUTO_TEST_CASE(converts_utf8_paths_without_using_the_system_code_page)
{
    const NomadString path = "日本語/école";

    BOOST_TEST(pathToString(pathFromString(path)) == path);
}

BOOST_AUTO_TEST_CASE(loads_typed_project_configuration)
{
    TestDirectory directory("nomad_project_configuration_load");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, COMPLETE_CONFIGURATION);

    const auto configuration = loadProjectConfiguration(projectFile);

    BOOST_TEST(configuration.schema == 1U);
    BOOST_TEST(configuration.project.name == "Test Game");
    BOOST_TEST(configuration.project.identifier == "com.example.test-game");
    BOOST_TEST(configuration.project.version == "0.1.0");
    BOOST_TEST(configuration.project.executable == "test-game");
    BOOST_TEST(configuration.project.entry == "start");
    BOOST_TEST(configuration.nomad.version == NomadVersion(0, 1, 0));
    BOOST_TEST(configuration.resources.directory == std::filesystem::path("res"));
    BOOST_TEST(configuration.package.output == std::filesystem::path("dist"));
    BOOST_REQUIRE(configuration.package.exclude.size() == 2U);
    BOOST_TEST(configuration.package.exclude[0] == "**/*.psd");
    BOOST_TEST(configuration.package.exclude[1] == "development/**");
    BOOST_TEST(configuration.root == std::filesystem::absolute(directory.getPath()));
    BOOST_TEST(
        resolveProjectResourcePath(configuration) ==
        (std::filesystem::absolute(directory.getPath()) / "res").lexically_normal()
    );
}

BOOST_AUTO_TEST_CASE(defaults_the_entry_function_to_init)
{
    TestDirectory directory("nomad_project_configuration_default_entry");
    auto configurationText = std::string(COMPLETE_CONFIGURATION);
    configurationText.erase(configurationText.find("entry = \"start\"\n"), 16U);
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, configurationText);

    const auto configuration = loadProjectConfiguration(projectFile);

    BOOST_TEST(configuration.project.entry == "init");
}

BOOST_AUTO_TEST_CASE(discovers_the_project_from_a_nested_path)
{
    TestDirectory directory("nomad_project_configuration_discovery");
    directory.write(NOMAD_PROJECT_FILE_NAME, COMPLETE_CONFIGURATION);
    const auto source = directory.write("res/scripts/start.nomad", "return 0");

    const auto root = findProjectRoot(source);
    BOOST_REQUIRE(root.has_value());
    BOOST_TEST(*root == std::filesystem::absolute(directory.getPath()));

    const auto configuration = discoverProjectConfiguration(source.parent_path());
    BOOST_TEST(configuration.project.name == "Test Game");
}

BOOST_AUTO_TEST_CASE(reports_invalid_schema_and_required_fields)
{
    TestDirectory directory("nomad_project_configuration_validation");
    auto unsupportedSchema = std::string(COMPLETE_CONFIGURATION);
    unsupportedSchema.replace(unsupportedSchema.find("schema = 1"), 10U, "schema = 2");
    const auto unsupportedFile = directory.write("unsupported.toml", unsupportedSchema);

    BOOST_CHECK_EXCEPTION(
        (void)loadProjectConfiguration(unsupportedFile),
        ProjectConfigurationError,
        [](const ProjectConfigurationError& error) {
            return std::string(error.what()).find("Unsupported project schema 2") != std::string::npos;
        }
    );

    auto missingName = std::string(COMPLETE_CONFIGURATION);
    missingName.erase(missingName.find("name = \"Test Game\"\n"), 19U);
    const auto missingNameFile = directory.write("missing-name.toml", missingName);

    BOOST_CHECK_EXCEPTION(
        (void)loadProjectConfiguration(missingNameFile),
        ProjectConfigurationError,
        [](const ProjectConfigurationError& error) {
            return std::string(error.what()).find("project.name") != std::string::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(rejects_invalid_nomad_versions)
{
    TestDirectory directory("nomad_project_configuration_version");
    auto invalidVersion = std::string(COMPLETE_CONFIGURATION);
    invalidVersion.replace(
        invalidVersion.find("version = \"0.1.0\"", invalidVersion.find("[nomad]")),
        17U,
        "version = \"0.1\""
    );
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, invalidVersion);

    BOOST_CHECK_EXCEPTION(
        (void)loadProjectConfiguration(projectFile),
        ProjectConfigurationError,
        [](const ProjectConfigurationError& error) {
            return std::string(error.what()).find("nomad.version") != std::string::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(validates_nomad_version_compatibility)
{
    BOOST_CHECK_NO_THROW(
        validateNomadVersionCompatibility(NomadVersion(1, 2, 3), NomadVersion(1, 2, 3))
    );
    BOOST_CHECK_NO_THROW(
        validateNomadVersionCompatibility(NomadVersion(1, 2, 3), NomadVersion(1, 3, 0))
    );
    BOOST_CHECK_EXCEPTION(
        validateNomadVersionCompatibility(NomadVersion(1, 3, 0), NomadVersion(1, 2, 3)),
        ProjectConfigurationError,
        [](const ProjectConfigurationError& error) {
            const auto message = std::string(error.what());
            return
                message.find("requires Nomad 1.3.0") != std::string::npos &&
                message.find("provides Nomad 1.2.3") != std::string::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(rejects_invalid_executable_names_and_exclusions)
{
    TestDirectory directory("nomad_project_configuration_values");
    auto executableWithExtension = std::string(COMPLETE_CONFIGURATION);
    executableWithExtension.replace(
        executableWithExtension.find("executable = \"test-game\""),
        24U,
        "executable = \"test-game.exe\""
    );
    const auto executableFile = directory.write("executable.toml", executableWithExtension);

    BOOST_CHECK_EXCEPTION(
        (void)loadProjectConfiguration(executableFile),
        ProjectConfigurationError,
        [](const ProjectConfigurationError& error) {
            return std::string(error.what()).find("extensionless file name") != std::string::npos;
        }
    );

    auto invalidExclusions = std::string(COMPLETE_CONFIGURATION);
    invalidExclusions.replace(
        invalidExclusions.find("exclude = [\"**/*.psd\", \"development/**\"]"),
        43U,
        "exclude = [\"**/*.psd\", 42]"
    );
    const auto exclusionsFile = directory.write("exclusions.toml", invalidExclusions);

    BOOST_CHECK_EXCEPTION(
        (void)loadProjectConfiguration(exclusionsFile),
        ProjectConfigurationError,
        [](const ProjectConfigurationError& error) {
            return std::string(error.what()).find("package.exclude") != std::string::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(reports_parse_locations_and_missing_projects)
{
    TestDirectory directory("nomad_project_configuration_errors");
    const auto malformed = directory.write(NOMAD_PROJECT_FILE_NAME, "schema = [\n");

    BOOST_CHECK_EXCEPTION(
        (void)loadProjectConfiguration(malformed),
        ProjectConfigurationError,
        [](const ProjectConfigurationError& error) {
            const auto message = std::string(error.what());
            return message.find("Failed to parse project configuration at 1:") != std::string::npos;
        }
    );

    TestDirectory missingDirectory("nomad_project_configuration_missing");
    BOOST_TEST(!findProjectRoot(missingDirectory.getPath()).has_value());
    BOOST_CHECK_THROW(
        (void)discoverProjectConfiguration(missingDirectory.getPath()),
        ProjectConfigurationError
    );
}

BOOST_AUTO_TEST_SUITE_END()
