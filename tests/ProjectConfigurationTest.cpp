// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectConfiguration.hpp>
#include <nomad/system/Path.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <string>
#include <utility>

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

[game]
debug = true

[audio]
directories = ["audio", "mod/audio"]

[text]
directories = ["text/text.csv", "text/expansion.csv"]

[sprites]
directories = ["sprites", "mod/sprites"]
max-texture-size = 1024
padding = 2

[resources]
directory = "res"

[window]
title = "Test Window"
resolution = [1280, 720]
size = [1600, 900]
fullscreen = true
splash-image = "images/splash.png"

[package]
output = "dist"
exclude = ["**/*.psd", "development/**"]
)";

} // namespace

BOOST_AUTO_TEST_SUITE(project_configuration)

BOOST_AUTO_TEST_CASE(converts_utf8_paths_without_using_the_system_code_page)
{
    const NomadString path = "日本語/école";

    BOOST_TEST(pathToUtf8(pathFromUtf8(path)) == path);
}

BOOST_AUTO_TEST_CASE(loads_typed_project_configuration)
{
    TestDirectory directory("nomad_project_configuration_load");
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, COMPLETE_CONFIGURATION);

    const auto configuration = loadNomadProject(projectFile);

    BOOST_TEST(configuration.configuration.schema == 1U);
    BOOST_TEST(
        static_cast<int>(configuration.configuration.type) ==
        static_cast<int>(ProjectConfigurationType::Development)
    );
    BOOST_TEST(configuration.configuration.project.name == "Test Game");
    BOOST_TEST(configuration.configuration.project.identifier == "com.example.test-game");
    BOOST_TEST(configuration.configuration.project.version == "0.1.0");
    BOOST_TEST(configuration.configuration.project.executable == "test-game");
    BOOST_TEST(configuration.gameOptions.entryFunction == "start");
    BOOST_TEST(configuration.configuration.sdk.version == NomadVersion(0, 1, 0));
    BOOST_TEST(configuration.configuration.resources.directory == std::filesystem::path("res"));
    BOOST_REQUIRE(configuration.gameOptions.debug.has_value());
    BOOST_TEST(*configuration.gameOptions.debug);
    BOOST_REQUIRE(configuration.gameOptions.title.has_value());
    BOOST_TEST(*configuration.gameOptions.title == "Test Window");
    BOOST_REQUIRE(configuration.gameOptions.resolution.has_value());
    BOOST_TEST(configuration.gameOptions.resolution->getX() == 1280);
    BOOST_TEST(configuration.gameOptions.resolution->getY() == 720);
    BOOST_REQUIRE(configuration.gameOptions.size.has_value());
    BOOST_TEST(configuration.gameOptions.size->getX() == 1600);
    BOOST_TEST(configuration.gameOptions.size->getY() == 900);
    BOOST_REQUIRE(configuration.gameOptions.fullscreen.has_value());
    BOOST_TEST(*configuration.gameOptions.fullscreen);
    BOOST_REQUIRE(configuration.gameOptions.splashImage.has_value());
    BOOST_TEST(*configuration.gameOptions.splashImage == NomadPath("images/splash.png"));
    BOOST_REQUIRE(configuration.gameOptions.audio.directories.has_value());
    BOOST_REQUIRE(configuration.gameOptions.audio.directories->size() == 2U);
    BOOST_TEST((*configuration.gameOptions.audio.directories)[0] == NomadPath("audio"));
    BOOST_TEST((*configuration.gameOptions.audio.directories)[1] == NomadPath("mod/audio"));
    BOOST_REQUIRE(configuration.gameOptions.text.directories.has_value());
    BOOST_REQUIRE(configuration.gameOptions.text.directories->size() == 2U);
    BOOST_TEST((*configuration.gameOptions.text.directories)[0] == NomadPath("text/text.csv"));
    BOOST_TEST((*configuration.gameOptions.text.directories)[1] == NomadPath("text/expansion.csv"));
    BOOST_REQUIRE(configuration.gameOptions.sprites.directories.has_value());
    BOOST_REQUIRE(configuration.gameOptions.sprites.directories->size() == 2U);
    BOOST_TEST((*configuration.gameOptions.sprites.directories)[0] == NomadPath("sprites"));
    BOOST_TEST((*configuration.gameOptions.sprites.directories)[1] == NomadPath("mod/sprites"));
    BOOST_REQUIRE(configuration.gameOptions.sprites.maxTextureSize.has_value());
    BOOST_TEST(*configuration.gameOptions.sprites.maxTextureSize == 1024);
    BOOST_REQUIRE(configuration.gameOptions.sprites.padding.has_value());
    BOOST_TEST(*configuration.gameOptions.sprites.padding == 2);
    BOOST_TEST(configuration.configuration.package.output == std::filesystem::path("dist"));
    BOOST_REQUIRE(configuration.configuration.package.exclude.size() == 2U);
    BOOST_TEST(configuration.configuration.package.exclude[0] == "**/*.psd");
    BOOST_TEST(configuration.configuration.package.exclude[1] == "development/**");
    BOOST_TEST(configuration.root == std::filesystem::absolute(directory.getPath()));
    BOOST_TEST(
        resolveProjectResourcePath(configuration) ==
        (std::filesystem::absolute(directory.getPath()) / "res").lexically_normal()
    );
}

BOOST_AUTO_TEST_CASE(loads_and_serializes_release_configuration)
{
    TestDirectory directory("nomad_release_project_configuration");
    const auto developmentFile = directory.write(NOMAD_PROJECT_FILE_NAME, COMPLETE_CONFIGURATION);
    const auto development = loadNomadProject(developmentFile);
    const auto releaseText = serializeReleaseProject(development);
    const auto releaseFile = directory.write("release/nomad.toml", releaseText);

    BOOST_TEST(releaseText.find("executable") == NomadString::npos);
    BOOST_TEST(releaseText.find("[resources]") == NomadString::npos);
    BOOST_TEST(releaseText.find("[package]") == NomadString::npos);
    BOOST_TEST(releaseText.find("[window]") != NomadString::npos);
    BOOST_TEST(releaseText.find("[audio]") != NomadString::npos);
    BOOST_TEST(releaseText.find("[text]") != NomadString::npos);
    BOOST_TEST(releaseText.find("[sprites]") != NomadString::npos);

    const auto release = loadNomadProject(releaseFile);
    BOOST_TEST(
        static_cast<int>(release.configuration.type) ==
        static_cast<int>(ProjectConfigurationType::Release)
    );
    BOOST_TEST(release.configuration.project.name == development.configuration.project.name);
    BOOST_TEST(release.configuration.project.identifier == development.configuration.project.identifier);
    BOOST_TEST(release.configuration.project.version == development.configuration.project.version);
    BOOST_TEST(release.gameOptions.entryFunction == development.gameOptions.entryFunction);
    BOOST_TEST(release.configuration.project.executable.empty());
    BOOST_TEST(release.configuration.sdk.version == development.configuration.sdk.version);
    BOOST_TEST(release.configuration.resources.directory == NomadPath("res"));
    BOOST_REQUIRE(release.gameOptions.debug.has_value());
    BOOST_REQUIRE(development.gameOptions.debug.has_value());
    BOOST_TEST(*release.gameOptions.debug == *development.gameOptions.debug);
    BOOST_REQUIRE(release.gameOptions.title.has_value());
    BOOST_REQUIRE(development.gameOptions.title.has_value());
    BOOST_TEST(*release.gameOptions.title == *development.gameOptions.title);
    BOOST_REQUIRE(release.gameOptions.resolution.has_value());
    BOOST_REQUIRE(development.gameOptions.resolution.has_value());
    BOOST_TEST(release.gameOptions.resolution->getX() == development.gameOptions.resolution->getX());
    BOOST_TEST(release.gameOptions.resolution->getY() == development.gameOptions.resolution->getY());
    BOOST_REQUIRE(release.gameOptions.size.has_value());
    BOOST_REQUIRE(development.gameOptions.size.has_value());
    BOOST_TEST(release.gameOptions.size->getX() == development.gameOptions.size->getX());
    BOOST_TEST(release.gameOptions.size->getY() == development.gameOptions.size->getY());
    BOOST_REQUIRE(release.gameOptions.fullscreen.has_value());
    BOOST_REQUIRE(development.gameOptions.fullscreen.has_value());
    BOOST_TEST(*release.gameOptions.fullscreen == *development.gameOptions.fullscreen);
    BOOST_REQUIRE(release.gameOptions.splashImage.has_value());
    BOOST_REQUIRE(development.gameOptions.splashImage.has_value());
    BOOST_TEST(*release.gameOptions.splashImage == *development.gameOptions.splashImage);
    BOOST_REQUIRE(release.gameOptions.audio.directories.has_value());
    BOOST_REQUIRE(development.gameOptions.audio.directories.has_value());
    BOOST_REQUIRE(
        release.gameOptions.audio.directories->size() ==
        development.gameOptions.audio.directories->size()
    );
    BOOST_TEST(
        (*release.gameOptions.audio.directories)[0] ==
        (*development.gameOptions.audio.directories)[0]
    );
    BOOST_TEST(
        (*release.gameOptions.audio.directories)[1] ==
        (*development.gameOptions.audio.directories)[1]
    );
    BOOST_REQUIRE(release.gameOptions.text.directories.has_value());
    BOOST_REQUIRE(development.gameOptions.text.directories.has_value());
    BOOST_TEST(
        *release.gameOptions.text.directories ==
        *development.gameOptions.text.directories
    );
    BOOST_REQUIRE(release.gameOptions.sprites.directories.has_value());
    BOOST_REQUIRE(development.gameOptions.sprites.directories.has_value());
    BOOST_REQUIRE(
        release.gameOptions.sprites.directories->size() ==
        development.gameOptions.sprites.directories->size()
    );
    BOOST_TEST(
        (*release.gameOptions.sprites.directories)[0] ==
        (*development.gameOptions.sprites.directories)[0]
    );
    BOOST_TEST(
        (*release.gameOptions.sprites.directories)[1] ==
        (*development.gameOptions.sprites.directories)[1]
    );
    BOOST_REQUIRE(release.gameOptions.sprites.maxTextureSize.has_value());
    BOOST_REQUIRE(development.gameOptions.sprites.maxTextureSize.has_value());
    BOOST_TEST(
        *release.gameOptions.sprites.maxTextureSize ==
        *development.gameOptions.sprites.maxTextureSize
    );
    BOOST_REQUIRE(release.gameOptions.sprites.padding.has_value());
    BOOST_REQUIRE(development.gameOptions.sprites.padding.has_value());
    BOOST_TEST(*release.gameOptions.sprites.padding == *development.gameOptions.sprites.padding);
    BOOST_TEST(release.configuration.package.output.empty());
    BOOST_TEST(release.configuration.package.exclude.empty());
}

BOOST_AUTO_TEST_CASE(defaults_and_disables_sprite_directories)
{
    TestDirectory directory("nomad_project_configuration_sprite_defaults");
    auto defaultText = std::string(COMPLETE_CONFIGURATION);
    const auto sprites = defaultText.find("[sprites]");
    const auto resources = defaultText.find("[resources]");
    defaultText.erase(sprites, resources - sprites);
    const auto defaultFile = directory.write("default.toml", defaultText);

    const auto defaults = loadNomadProject(defaultFile);
    BOOST_TEST(!defaults.gameOptions.sprites.directories.has_value());
    BOOST_TEST(!defaults.gameOptions.sprites.maxTextureSize.has_value());
    BOOST_TEST(!defaults.gameOptions.sprites.padding.has_value());
    BOOST_TEST(serializeReleaseProject(defaults).find("[sprites]") == NomadString::npos);

    auto disabledText = defaultText;
    disabledText += "\n[sprites]\ndirectories = []\n";
    const auto disabledFile = directory.write("disabled.toml", disabledText);
    const auto disabled = loadNomadProject(disabledFile);

    BOOST_REQUIRE(disabled.gameOptions.sprites.directories.has_value());
    BOOST_TEST(disabled.gameOptions.sprites.directories->empty());
    BOOST_TEST(!disabled.gameOptions.sprites.maxTextureSize.has_value());
    BOOST_TEST(!disabled.gameOptions.sprites.padding.has_value());
    BOOST_TEST(serializeReleaseProject(disabled).find("directories = []") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(defaults_and_disables_audio_directories)
{
    TestDirectory directory("nomad_project_configuration_audio_defaults");
    auto defaultText = std::string(COMPLETE_CONFIGURATION);
    const auto audio = defaultText.find("[audio]");
    const auto sprites = defaultText.find("[sprites]");
    defaultText.erase(audio, sprites - audio);
    const auto defaultFile = directory.write("default.toml", defaultText);

    const auto defaults = loadNomadProject(defaultFile);
    BOOST_TEST(!defaults.gameOptions.audio.directories.has_value());
    BOOST_TEST(serializeReleaseProject(defaults).find("[audio]") == NomadString::npos);

    auto disabledText = defaultText;
    disabledText += "\n[audio]\ndirectories = []\n";
    const auto disabledFile = directory.write("disabled.toml", disabledText);
    const auto disabled = loadNomadProject(disabledFile);

    BOOST_REQUIRE(disabled.gameOptions.audio.directories.has_value());
    BOOST_TEST(disabled.gameOptions.audio.directories->empty());
    BOOST_TEST(serializeReleaseProject(disabled).find("[audio]") != NomadString::npos);
    BOOST_TEST(serializeReleaseProject(disabled).find("directories = []") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(normalizes_audio_directories)
{
    TestDirectory directory("nomad_project_configuration_audio_normalization");
    auto configurationText = std::string(COMPLETE_CONFIGURATION);
    const auto originalDirectories = std::string("directories = [\"audio\", \"mod/audio\"]");
    configurationText.replace(
        configurationText.find(originalDirectories),
        originalDirectories.size(),
        "directories = [\"audio/effects/..\", \"mod\\\\audio\"]"
    );
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, configurationText);

    const auto configuration = loadNomadProject(projectFile);

    BOOST_REQUIRE(configuration.gameOptions.audio.directories.has_value());
    BOOST_REQUIRE(configuration.gameOptions.audio.directories->size() == 2U);
    BOOST_TEST((*configuration.gameOptions.audio.directories)[0] == NomadPath("audio"));
    BOOST_TEST((*configuration.gameOptions.audio.directories)[1] == NomadPath("mod/audio"));
}

BOOST_AUTO_TEST_CASE(rejects_invalid_audio_directories)
{
    TestDirectory directory("nomad_project_configuration_audio_validation");

    const auto originalDirectories = std::string("[\"audio\", \"mod/audio\"]");
    NomadIndex caseIndex = 0;

    for (const auto& directories : {
        "false",
        "[\"audio\", 42]",
        "[\"\"]",
        "[\".\"]",
        "[\"../audio\"]",
        "[\"audio\", \"audio/../audio\"]",
        "[\"audio\", \"audio/effects\"]",
        "[\"Audio\", \"audio\"]"
    }) {
        auto configurationText = std::string(COMPLETE_CONFIGURATION);
        const auto value = configurationText.find(originalDirectories);
        configurationText.replace(value, originalDirectories.size(), directories);
        const auto projectFile = directory.write(
            "invalid-audio-" + std::to_string(caseIndex++) + ".toml",
            configurationText
        );

        BOOST_CHECK_EXCEPTION(
            (void)loadNomadProject(projectFile),
            ProjectConfigurationError,
            [](const ProjectConfigurationError& error) {
                return NomadString(error.what()).find("audio.directories") != NomadString::npos;
            }
        );
    }
}

BOOST_AUTO_TEST_CASE(parses_and_serializes_independent_sprite_packing_settings)
{
    TestDirectory directory("nomad_project_configuration_sprite_packing");
    auto configurationText = std::string(COMPLETE_CONFIGURATION);
    const auto sprites = configurationText.find("[sprites]");
    const auto resources = configurationText.find("[resources]");
    configurationText.replace(
        sprites,
        resources - sprites,
        "[sprites]\nmax-texture-size = 4096\npadding = 0\n\n"
    );
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, configurationText);

    const auto configuration = loadNomadProject(projectFile);
    BOOST_TEST(!configuration.gameOptions.sprites.directories.has_value());
    BOOST_REQUIRE(configuration.gameOptions.sprites.maxTextureSize.has_value());
    BOOST_TEST(*configuration.gameOptions.sprites.maxTextureSize == 4096);
    BOOST_REQUIRE(configuration.gameOptions.sprites.padding.has_value());
    BOOST_TEST(*configuration.gameOptions.sprites.padding == 0);

    const auto releaseText = serializeReleaseProject(configuration);
    BOOST_TEST(releaseText.find("[sprites]\ndirectories") == NomadString::npos);
    BOOST_TEST(releaseText.find("max-texture-size = 4096") != NomadString::npos);
    BOOST_TEST(releaseText.find("padding = 0") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(normalizes_sprite_directories)
{
    TestDirectory directory("nomad_project_configuration_sprite_normalization");
    auto configurationText = std::string(COMPLETE_CONFIGURATION);
    const auto originalDirectories = std::string("directories = [\"sprites\", \"mod/sprites\"]");
    configurationText.replace(
        configurationText.find(originalDirectories),
        originalDirectories.size(),
        "directories = [\"sprites/characters/..\", \"mod\\\\sprites\"]"
    );
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, configurationText);

    const auto configuration = loadNomadProject(projectFile);

    BOOST_REQUIRE(configuration.gameOptions.sprites.directories.has_value());
    BOOST_REQUIRE(configuration.gameOptions.sprites.directories->size() == 2U);
    BOOST_TEST((*configuration.gameOptions.sprites.directories)[0] == NomadPath("sprites"));
    BOOST_TEST((*configuration.gameOptions.sprites.directories)[1] == NomadPath("mod/sprites"));
}

BOOST_AUTO_TEST_CASE(rejects_invalid_sprite_directories)
{
    TestDirectory directory("nomad_project_configuration_sprite_validation");

    const auto originalDirectories = std::string("[\"sprites\", \"mod/sprites\"]");
    NomadIndex caseIndex = 0;

    for (const auto& directories : {
        "false",
        "[\"sprites\", 42]",
        "[\"\"]",
        "[\".\"]",
        "[\"../sprites\"]",
        "[\"sprites\", \"sprites/../sprites\"]",
        "[\"sprites\", \"sprites/ui\"]",
        "[\"Sprites\", \"sprites\"]"
    }) {
        auto configurationText = std::string(COMPLETE_CONFIGURATION);
        const auto value = configurationText.find(originalDirectories);
        configurationText.replace(value, originalDirectories.size(), directories);
        const auto projectFile = directory.write("invalid-" + std::to_string(caseIndex++) + ".toml", configurationText);

        BOOST_CHECK_EXCEPTION(
            (void)loadNomadProject(projectFile),
            ProjectConfigurationError,
            [](const ProjectConfigurationError& error) {
                return NomadString(error.what()).find("sprites.directories") != NomadString::npos;
            }
        );
    }
}

BOOST_AUTO_TEST_CASE(rejects_partial_development_configuration)
{
    TestDirectory directory("nomad_partial_project_configuration");
    auto configurationText = std::string(COMPLETE_CONFIGURATION);
    const auto executable = configurationText.find("executable = \"test-game\"\n");
    configurationText.erase(executable, std::string("executable = \"test-game\"\n").size());
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, configurationText);

    BOOST_CHECK_EXCEPTION(
        (void)loadNomadProject(projectFile),
        ProjectConfigurationError,
        [](const ProjectConfigurationError& error) {
            return NomadString(error.what()).find("requires 'project.executable'") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(defaults_optional_development_fields)
{
    TestDirectory directory("nomad_project_configuration_defaults");
    auto configurationText = std::string(COMPLETE_CONFIGURATION);
    configurationText.erase(configurationText.find("[resources]"));
    const auto entry = configurationText.find("entry = \"start\"\n");
    configurationText.erase(entry, std::string("entry = \"start\"\n").size());

    for (const auto& tables : {"", "\n[resources]\n[package]\n",
        "\n[resources]\ndirectory = \"assets\"\n[package]\noutput = \"packages\"\n"}) {
        const auto file = directory.write(NOMAD_PROJECT_FILE_NAME, configurationText + tables);
        const auto configuration = loadNomadProject(file);
        BOOST_TEST(
            static_cast<int>(configuration.configuration.type) ==
            static_cast<int>(ProjectConfigurationType::Development)
        );
        BOOST_TEST(configuration.gameOptions.entryFunction == "init");
        const auto overrides = std::string(tables).find("assets") != std::string::npos;
        BOOST_TEST(
            configuration.configuration.resources.directory ==
            NomadPath(overrides ? "assets" : "res")
        );
        BOOST_TEST(
            configuration.configuration.package.output ==
            NomadPath(overrides ? "packages" : "dist")
        );
        BOOST_TEST(configuration.configuration.package.exclude.empty());
    }

    const auto file = directory.write(NOMAD_PROJECT_FILE_NAME, configurationText + "\n[package]\nexclude = []\n");
    BOOST_TEST(loadNomadProject(file).configuration.package.exclude.empty());
}

BOOST_AUTO_TEST_CASE(defaults_window_configuration)
{
    TestDirectory directory("nomad_project_configuration_window_defaults");
    auto configurationText = std::string(COMPLETE_CONFIGURATION);
    const auto window = configurationText.find("[window]");
    const auto package = configurationText.find("[package]");
    configurationText.erase(window, package - window);
    const auto file = directory.write(NOMAD_PROJECT_FILE_NAME, configurationText);

    const auto configuration = loadNomadProject(file);

    BOOST_TEST(!configuration.gameOptions.title.has_value());
    BOOST_TEST(!configuration.gameOptions.resolution.has_value());
    BOOST_TEST(!configuration.gameOptions.size.has_value());
    BOOST_TEST(!configuration.gameOptions.fullscreen.has_value());
    BOOST_TEST(!configuration.gameOptions.splashImage.has_value());
    BOOST_TEST(serializeReleaseProject(configuration).find("[window]") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(leaves_debug_unconfigured_when_game_table_is_omitted)
{
    TestDirectory directory("nomad_project_configuration_game_defaults");
    auto configurationText = std::string(COMPLETE_CONFIGURATION);
    const auto game = configurationText.find("[game]");
    const auto sprites = configurationText.find("[sprites]");
    configurationText.erase(game, sprites - game);
    const auto file = directory.write(NOMAD_PROJECT_FILE_NAME, configurationText);

    const auto configuration = loadNomadProject(file);

    BOOST_TEST(!configuration.gameOptions.debug.has_value());
    BOOST_TEST(serializeReleaseProject(configuration).find("[game]") == NomadString::npos);
}

BOOST_AUTO_TEST_CASE(leaves_window_size_unconfigured_when_omitted)
{
    TestDirectory directory("nomad_project_configuration_window_size_fallback");
    auto configurationText = std::string(COMPLETE_CONFIGURATION);
    configurationText.erase(
        configurationText.find("size = [1600, 900]\n"),
        std::string("size = [1600, 900]\n").size()
    );
    const auto file = directory.write(NOMAD_PROJECT_FILE_NAME, configurationText);

    const auto configuration = loadNomadProject(file);

    BOOST_REQUIRE(configuration.gameOptions.resolution.has_value());
    BOOST_TEST(configuration.gameOptions.resolution->getX() == 1280);
    BOOST_TEST(configuration.gameOptions.resolution->getY() == 720);
    BOOST_TEST(!configuration.gameOptions.size.has_value());
}

BOOST_AUTO_TEST_CASE(rejects_invalid_optional_fields_instead_of_defaulting)
{
    TestDirectory directory("nomad_project_configuration_optional_validation");
    for (const auto& replacement : {
        std::pair{"entry = \"start\"", "entry = 42"},
        std::pair{"entry = \"start\"", "entry = \"\""},
        std::pair{"directory = \"res\"", "directory = false"},
        std::pair{"directory = \"res\"", "directory = \"\""},
        std::pair{"debug = true", "debug = \"yes\""},
        std::pair{"title = \"Test Window\"", "title = \"\""},
        std::pair{"resolution = [1280, 720]", "resolution = [1280]"},
        std::pair{"resolution = [1280, 720]", "resolution = [1280, 0]"},
        std::pair{"size = [1600, 900]", "size = [1600]"},
        std::pair{"size = [1600, 900]", "size = [-1, 900]"},
        std::pair{"fullscreen = true", "fullscreen = \"yes\""},
        std::pair{"splash-image = \"images/splash.png\"", "splash-image = \"../splash.png\""},
        std::pair{"output = \"dist\"", "output = 42"},
        std::pair{"output = \"dist\"", "output = \"\""},
        std::pair{"directories = [\"sprites\", \"mod/sprites\"]", "directories = \"sprites\""},
        std::pair{"max-texture-size = 1024", "max-texture-size = 0"},
        std::pair{"max-texture-size = 1024", "max-texture-size = \"large\""},
        std::pair{"padding = 2", "padding = -1"},
        std::pair{"padding = 2", "padding = false"},
        std::pair{"exclude = [\"**/*.psd\", \"development/**\"]", "exclude = false"}
    }) {
        auto text = std::string(COMPLETE_CONFIGURATION);
        text.replace(text.find(replacement.first), std::string(replacement.first).size(), replacement.second);
        const auto file = directory.write(NOMAD_PROJECT_FILE_NAME, text);
        BOOST_TEST_CONTEXT("replacement: " << replacement.first << " -> " << replacement.second) {
            BOOST_CHECK_THROW((void)loadNomadProject(file), ProjectConfigurationError);
        }
    }
}

BOOST_AUTO_TEST_CASE(defaults_the_entry_function_to_init)
{
    TestDirectory directory("nomad_project_configuration_default_entry");
    auto configurationText = std::string(COMPLETE_CONFIGURATION);
    configurationText.erase(configurationText.find("entry = \"start\"\n"), 16U);
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, configurationText);

    const auto configuration = loadNomadProject(projectFile);

    BOOST_TEST(configuration.gameOptions.entryFunction == "init");
}

BOOST_AUTO_TEST_CASE(discovers_the_project_from_a_nested_path)
{
    TestDirectory directory("nomad_project_configuration_discovery");
    directory.write(NOMAD_PROJECT_FILE_NAME, COMPLETE_CONFIGURATION);
    const auto source = directory.write("res/scripts/start.nomad", "return 0");

    const auto root = findProjectRoot(source);
    BOOST_REQUIRE(root.has_value());
    BOOST_TEST(*root == std::filesystem::absolute(directory.getPath()));

    const auto configuration = discoverNomadProject(source.parent_path());
    BOOST_TEST(configuration.configuration.project.name == "Test Game");
}

BOOST_AUTO_TEST_CASE(reports_invalid_schema_and_required_fields)
{
    TestDirectory directory("nomad_project_configuration_validation");
    auto unsupportedSchema = std::string(COMPLETE_CONFIGURATION);
    unsupportedSchema.replace(unsupportedSchema.find("schema = 1"), 10U, "schema = 2");
    const auto unsupportedFile = directory.write("unsupported.toml", unsupportedSchema);

    BOOST_CHECK_EXCEPTION(
        (void)loadNomadProject(unsupportedFile),
        ProjectConfigurationError,
        [](const ProjectConfigurationError& error) {
            return std::string(error.what()).find("Unsupported project schema 2") != std::string::npos;
        }
    );

    auto missingName = std::string(COMPLETE_CONFIGURATION);
    missingName.erase(missingName.find("name = \"Test Game\"\n"), 19U);
    const auto missingNameFile = directory.write("missing-name.toml", missingName);

    BOOST_CHECK_EXCEPTION(
        (void)loadNomadProject(missingNameFile),
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
        (void)loadNomadProject(projectFile),
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
        (void)loadNomadProject(executableFile),
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
        (void)loadNomadProject(exclusionsFile),
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
        (void)loadNomadProject(malformed),
        ProjectConfigurationError,
        [](const ProjectConfigurationError& error) {
            const auto message = std::string(error.what());
            return message.find("Failed to parse project configuration at 1:") != std::string::npos;
        }
    );

    TestDirectory missingDirectory("nomad_project_configuration_missing");
    BOOST_TEST(!findProjectRoot(missingDirectory.getPath()).has_value());
    BOOST_CHECK_THROW(
        (void)discoverNomadProject(missingDirectory.getPath()),
        ProjectConfigurationError
    );
}

BOOST_AUTO_TEST_SUITE_END()
