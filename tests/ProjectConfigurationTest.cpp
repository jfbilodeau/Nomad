// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/GameConfiguration.hpp>

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

[nomad]
version = "0.1.0"
modules = ["game"]

[game]
entry = "start"
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

BOOST_AUTO_TEST_CASE(project_loading_preserves_script_settings_for_the_compiler)
{
    const TestDirectory directory("nomad_project_preserves_scripts");
    for (const auto& [text, booleanField] : {
        std::pair{"scripts = false\n", "scripts"},
        std::pair{"[scripts]\ndirectories = false\n", "scripts.directories"},
        std::pair{"[scripts]\ndirectories = ['scripts\\nested\\..', 'mods']\n", ""}
    }) {
        auto document = NomadConfiguration::parse(COMPLETE_CONFIGURATION);
        document.merge(NomadConfiguration::parse(text));
        const auto project = loadNomadProject(directory.write(NOMAD_PROJECT_FILE_NAME, document.serialize()));
        const auto release = NomadConfiguration::parse(serializeReleaseProject(project));
        if (NomadStringView(booleanField).empty()) {
            BOOST_TEST(release.requireStringArray("scripts.directories") ==
                document.requireStringArray("scripts.directories"));
        } else {
            BOOST_TEST(!release.requireBoolean(booleanField));
        }
    }
}

BOOST_AUTO_TEST_CASE(requires_modules_and_accepts_explicit_module_lists)
{
    TestDirectory directory("nomad_project_configuration_modules");
    auto missingModules = NomadString(COMPLETE_CONFIGURATION);
    missingModules.erase(missingModules.find("modules = "), NomadString("modules = [\"game\"]\n").size());
    const auto legacyFile = directory.write("missing.toml", missingModules);
    BOOST_CHECK_EXCEPTION(
        (void)loadNomadProject(legacyFile),
        NomadConfigurationException,
        [](const auto& error) {
            return NomadString(error.what()).find("nomad.modules") != NomadString::npos;
        }
    );
    missingModules.erase(missingModules.find("executable = "), NomadString("executable = \"test-game\"\n").size());
    missingModules.erase(missingModules.find("[resources]"), missingModules.find("[window]") - missingModules.find("[resources]"));
    missingModules.erase(missingModules.find("[package]"));
    BOOST_CHECK_EXCEPTION(
        (void)loadNomadProject(directory.write("missing-release.toml", missingModules)),
        NomadConfigurationException,
        [](const auto& error) {
            return NomadString(error.what()).find("nomad.modules") != NomadString::npos;
        }
    );

    for (const auto& modules : {"[]", "[\"game\", \"custom-module\"]"}) {
        auto text = NomadString(COMPLETE_CONFIGURATION);
        text.replace(text.find("modules = "), NomadString("modules = [\"game\"]").size(),
            "modules = " + NomadString(modules));
        const auto file = directory.write("explicit.toml", text);
        const auto project = loadNomadProject(file);
        const auto release = NomadConfiguration::parse(serializeReleaseProject(project));
        BOOST_TEST(
            project.configuration.sdk.modules == project.document.requireStringArray("nomad.modules")
        );
        BOOST_TEST(
            release.requireStringArray("nomad.modules") == project.configuration.sdk.modules
        );
    }
}

BOOST_AUTO_TEST_CASE(rejects_invalid_module_types_names_and_duplicates)
{
    TestDirectory directory("nomad_project_configuration_invalid_modules");
    for (const auto& modules : {
        "false", "\"game\"", "[1]", "[\"game\", false]", "[\"\"]",
        "[\"Nomad-game\"]", "[\"../game\"]", "[\"nomad_game\"]",
        "[\"1module\"]", "[\"game\", \"game\"]"
    }) {
        auto text = NomadString(COMPLETE_CONFIGURATION);
        text.replace(text.find("modules = "), NomadString("modules = [\"game\"]").size(),
            "modules = " + NomadString(modules));
        const auto file = directory.write("invalid.toml", text);
        BOOST_CHECK_EXCEPTION(
            (void)loadNomadProject(file),
            NomadConfigurationException,
            [](const auto& error) {
                return NomadString(error.what()).find("nomad.modules") != NomadString::npos;
            }
        );
    }
}

BOOST_AUTO_TEST_CASE(preserves_module_settings_and_source_document_in_release)
{
    TestDirectory directory("nomad_project_configuration_document");
    auto text = NomadString(COMPLETE_CONFIGURATION);
    text.replace(text.find("modules = "), NomadString("modules = [\"game\"]").size(),
        "modules = [\"game\", \"custom-module\"]\ncustom = \"sdk-setting\"");
    text.insert(text.find("[project]") + NomadString("[project]").size(),
        "\ncustom = \"project-setting\"");
    text.insert(text.find("[window]") + NomadString("[window]").size(),
        "\ncustom = \"window-setting\"");
    text += R"(
[custom-module]
enabled = true
count = 9
names = ["one", "two"]
ratio = 1.5

[custom-module.settings]
label = "untouched"
)";
    const auto file = directory.write(NOMAD_PROJECT_FILE_NAME, text);
    auto project = loadNomadProject(file);
    const auto original = project.document.serialize();
    const auto releaseText = serializeReleaseProject(project);
    const auto release = NomadConfiguration::parse(releaseText);

    BOOST_TEST(project.document.serialize() == original);
    BOOST_TEST(project.document.contains("project.executable"));
    BOOST_TEST(project.document.contains("resources"));
    BOOST_TEST(project.document.contains("package"));
    BOOST_TEST(!release.contains("project.executable"));
    BOOST_TEST(release.contains("resources"));
    BOOST_TEST(!release.contains("package"));
    BOOST_TEST(release.requireString("project.custom") == "project-setting");
    BOOST_TEST(release.requireString("nomad.custom") == "sdk-setting");
    BOOST_TEST(release.requireString("window.custom") == "window-setting");
    BOOST_TEST(release.requireString("window.title") == "Test Window");
    BOOST_TEST(release.requireInteger("sprites.padding") == 2);
    BOOST_TEST(release.requireBoolean("custom-module.enabled"));
    BOOST_TEST(release.requireInteger("custom-module.count") == 9);
    BOOST_TEST(release.requireStringArray("custom-module.names").size() == 2U);
    BOOST_TEST(release.requireString("custom-module.settings.label") == "untouched");
    BOOST_TEST(releaseText.find("ratio = 1.5") != NomadString::npos);
    BOOST_TEST(release.requireStringArray("nomad.modules") == project.configuration.sdk.modules);
    const auto releaseFile = directory.write("release/nomad.toml", releaseText);
    const auto loadedRelease = loadNomadProject(releaseFile);
    BOOST_TEST(loadedRelease.configuration.sdk.modules == project.configuration.sdk.modules);
    BOOST_TEST(loadedRelease.document.requireString("custom-module.settings.label") == "untouched");
}

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
    const auto configurationOptions = readGameOptions(configuration.document);

    BOOST_TEST(configuration.configuration.schema == 1U);
    BOOST_TEST(
        static_cast<int>(configuration.configuration.type) ==
        static_cast<int>(ProjectConfigurationType::Development)
    );
    BOOST_TEST(configuration.configuration.project.name == "Test Game");
    BOOST_TEST(configuration.configuration.project.identifier == "com.example.test-game");
    BOOST_TEST(configuration.configuration.project.version == "0.1.0");
    BOOST_TEST(configuration.configuration.project.executable == "test-game");
    BOOST_TEST(configurationOptions.entryFunction == "start");
    BOOST_TEST(configuration.configuration.sdk.version == NomadVersion(0, 1, 0));
    BOOST_REQUIRE(configurationOptions.debug.has_value());
    BOOST_TEST(*configurationOptions.debug);
    BOOST_REQUIRE(configurationOptions.title.has_value());
    BOOST_TEST(*configurationOptions.title == "Test Window");
    BOOST_REQUIRE(configurationOptions.resolution.has_value());
    BOOST_TEST(configurationOptions.resolution->getX() == 1280);
    BOOST_TEST(configurationOptions.resolution->getY() == 720);
    BOOST_REQUIRE(configurationOptions.size.has_value());
    BOOST_TEST(configurationOptions.size->getX() == 1600);
    BOOST_TEST(configurationOptions.size->getY() == 900);
    BOOST_REQUIRE(configurationOptions.fullscreen.has_value());
    BOOST_TEST(*configurationOptions.fullscreen);
    BOOST_REQUIRE(configurationOptions.splashImage.has_value());
    BOOST_TEST(*configurationOptions.splashImage == NomadPath("images/splash.png"));
    BOOST_REQUIRE(configurationOptions.audio.directories.has_value());
    BOOST_REQUIRE(configurationOptions.audio.directories->size() == 2U);
    BOOST_TEST((*configurationOptions.audio.directories)[0] == NomadPath("audio"));
    BOOST_TEST((*configurationOptions.audio.directories)[1] == NomadPath("mod/audio"));
    BOOST_REQUIRE(configurationOptions.text.directories.has_value());
    BOOST_REQUIRE(configurationOptions.text.directories->size() == 2U);
    BOOST_TEST((*configurationOptions.text.directories)[0] == NomadPath("text/text.csv"));
    BOOST_TEST((*configurationOptions.text.directories)[1] == NomadPath("text/expansion.csv"));
    BOOST_REQUIRE(configurationOptions.sprites.directories.has_value());
    BOOST_REQUIRE(configurationOptions.sprites.directories->size() == 2U);
    BOOST_TEST((*configurationOptions.sprites.directories)[0] == NomadPath("sprites"));
    BOOST_TEST((*configurationOptions.sprites.directories)[1] == NomadPath("mod/sprites"));
    BOOST_REQUIRE(configurationOptions.sprites.maxTextureSize.has_value());
    BOOST_TEST(*configurationOptions.sprites.maxTextureSize == 1024);
    BOOST_REQUIRE(configurationOptions.sprites.padding.has_value());
    BOOST_TEST(*configurationOptions.sprites.padding == 2);
    BOOST_TEST(configuration.configuration.package.output == std::filesystem::path("dist"));
    BOOST_REQUIRE(configuration.configuration.package.exclude.size() == 2U);
    BOOST_TEST(configuration.configuration.package.exclude[0] == "**/*.psd");
    BOOST_TEST(configuration.configuration.package.exclude[1] == "development/**");
    BOOST_TEST(configuration.root == std::filesystem::absolute(directory.getPath()));
}

BOOST_AUTO_TEST_CASE(loads_and_serializes_release_configuration)
{
    TestDirectory directory("nomad_release_project_configuration");
    const auto developmentFile = directory.write(NOMAD_PROJECT_FILE_NAME, COMPLETE_CONFIGURATION);
    const auto development = loadNomadProject(developmentFile);
    const auto developmentOptions = readGameOptions(development.document);
    const auto releaseText = serializeReleaseProject(development);
    const auto releaseFile = directory.write("release/nomad.toml", releaseText);

    BOOST_TEST(releaseText.find("executable") == NomadString::npos);
    BOOST_TEST(releaseText.find("[resources]") != NomadString::npos);
    BOOST_TEST(releaseText.find("[package]") == NomadString::npos);
    BOOST_TEST(releaseText.find("[window]") != NomadString::npos);
    BOOST_TEST(releaseText.find("[audio]") != NomadString::npos);
    BOOST_TEST(releaseText.find("[text]") != NomadString::npos);
    BOOST_TEST(releaseText.find("[sprites]") != NomadString::npos);

    const auto release = loadNomadProject(releaseFile);
    const auto releaseOptions = readGameOptions(release.document);
    BOOST_TEST(
        static_cast<int>(release.configuration.type) ==
        static_cast<int>(ProjectConfigurationType::Release)
    );
    BOOST_TEST(release.configuration.project.name == development.configuration.project.name);
    BOOST_TEST(release.configuration.project.identifier == development.configuration.project.identifier);
    BOOST_TEST(release.configuration.project.version == development.configuration.project.version);
    BOOST_TEST(releaseOptions.entryFunction == developmentOptions.entryFunction);
    BOOST_TEST(release.configuration.project.executable.empty());
    BOOST_TEST(release.configuration.sdk.version == development.configuration.sdk.version);
    BOOST_REQUIRE(releaseOptions.debug.has_value());
    BOOST_REQUIRE(developmentOptions.debug.has_value());
    BOOST_TEST(*releaseOptions.debug == *developmentOptions.debug);
    BOOST_REQUIRE(releaseOptions.title.has_value());
    BOOST_REQUIRE(developmentOptions.title.has_value());
    BOOST_TEST(*releaseOptions.title == *developmentOptions.title);
    BOOST_REQUIRE(releaseOptions.resolution.has_value());
    BOOST_REQUIRE(developmentOptions.resolution.has_value());
    BOOST_TEST(releaseOptions.resolution->getX() == developmentOptions.resolution->getX());
    BOOST_TEST(releaseOptions.resolution->getY() == developmentOptions.resolution->getY());
    BOOST_REQUIRE(releaseOptions.size.has_value());
    BOOST_REQUIRE(developmentOptions.size.has_value());
    BOOST_TEST(releaseOptions.size->getX() == developmentOptions.size->getX());
    BOOST_TEST(releaseOptions.size->getY() == developmentOptions.size->getY());
    BOOST_REQUIRE(releaseOptions.fullscreen.has_value());
    BOOST_REQUIRE(developmentOptions.fullscreen.has_value());
    BOOST_TEST(*releaseOptions.fullscreen == *developmentOptions.fullscreen);
    BOOST_REQUIRE(releaseOptions.splashImage.has_value());
    BOOST_REQUIRE(developmentOptions.splashImage.has_value());
    BOOST_TEST(*releaseOptions.splashImage == *developmentOptions.splashImage);
    BOOST_REQUIRE(releaseOptions.audio.directories.has_value());
    BOOST_REQUIRE(developmentOptions.audio.directories.has_value());
    BOOST_REQUIRE(
        releaseOptions.audio.directories->size() ==
        developmentOptions.audio.directories->size()
    );
    BOOST_TEST(
        (*releaseOptions.audio.directories)[0] ==
        (*developmentOptions.audio.directories)[0]
    );
    BOOST_TEST(
        (*releaseOptions.audio.directories)[1] ==
        (*developmentOptions.audio.directories)[1]
    );
    BOOST_REQUIRE(releaseOptions.text.directories.has_value());
    BOOST_REQUIRE(developmentOptions.text.directories.has_value());
    BOOST_TEST(
        *releaseOptions.text.directories ==
        *developmentOptions.text.directories
    );
    BOOST_REQUIRE(releaseOptions.sprites.directories.has_value());
    BOOST_REQUIRE(developmentOptions.sprites.directories.has_value());
    BOOST_REQUIRE(
        releaseOptions.sprites.directories->size() ==
        developmentOptions.sprites.directories->size()
    );
    BOOST_TEST(
        (*releaseOptions.sprites.directories)[0] ==
        (*developmentOptions.sprites.directories)[0]
    );
    BOOST_TEST(
        (*releaseOptions.sprites.directories)[1] ==
        (*developmentOptions.sprites.directories)[1]
    );
    BOOST_REQUIRE(releaseOptions.sprites.maxTextureSize.has_value());
    BOOST_REQUIRE(developmentOptions.sprites.maxTextureSize.has_value());
    BOOST_TEST(
        *releaseOptions.sprites.maxTextureSize ==
        *developmentOptions.sprites.maxTextureSize
    );
    BOOST_REQUIRE(releaseOptions.sprites.padding.has_value());
    BOOST_REQUIRE(developmentOptions.sprites.padding.has_value());
    BOOST_TEST(*releaseOptions.sprites.padding == *developmentOptions.sprites.padding);
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
    const auto defaultsOptions = readGameOptions(defaults.document);
    BOOST_TEST(!defaultsOptions.sprites.directories.has_value());
    BOOST_TEST(!defaultsOptions.sprites.maxTextureSize.has_value());
    BOOST_TEST(!defaultsOptions.sprites.padding.has_value());
    BOOST_TEST(serializeReleaseProject(defaults).find("[sprites]") == NomadString::npos);

    auto disabledText = defaultText;
    disabledText += "\n[sprites]\ndirectories = []\n";
    const auto disabledFile = directory.write("disabled.toml", disabledText);
    const auto disabled = loadNomadProject(disabledFile);
    const auto disabledOptions = readGameOptions(disabled.document);

    BOOST_REQUIRE(disabledOptions.sprites.directories.has_value());
    BOOST_TEST(disabledOptions.sprites.directories->empty());
    BOOST_TEST(!disabledOptions.sprites.maxTextureSize.has_value());
    BOOST_TEST(!disabledOptions.sprites.padding.has_value());
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
    const auto defaultsOptions = readGameOptions(defaults.document);
    BOOST_TEST(!defaultsOptions.audio.directories.has_value());
    BOOST_TEST(serializeReleaseProject(defaults).find("[audio]") == NomadString::npos);

    auto disabledText = defaultText;
    disabledText += "\n[audio]\ndirectories = []\n";
    const auto disabledFile = directory.write("disabled.toml", disabledText);
    const auto disabled = loadNomadProject(disabledFile);
    const auto disabledOptions = readGameOptions(disabled.document);

    BOOST_REQUIRE(disabledOptions.audio.directories.has_value());
    BOOST_TEST(disabledOptions.audio.directories->empty());
    BOOST_TEST(serializeReleaseProject(disabled).find("[audio]") != NomadString::npos);
    BOOST_TEST(serializeReleaseProject(disabled).find("directories = []") != NomadString::npos);
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
    const auto configurationOptions = readGameOptions(configuration.document);
    BOOST_TEST(!configurationOptions.sprites.directories.has_value());
    BOOST_REQUIRE(configurationOptions.sprites.maxTextureSize.has_value());
    BOOST_TEST(*configurationOptions.sprites.maxTextureSize == 4096);
    BOOST_REQUIRE(configurationOptions.sprites.padding.has_value());
    BOOST_TEST(*configurationOptions.sprites.padding == 0);

    const auto releaseText = serializeReleaseProject(configuration);
    BOOST_TEST(releaseText.find("[sprites]\ndirectories") == NomadString::npos);
    BOOST_TEST(releaseText.find("max-texture-size = 4096") != NomadString::npos);
    BOOST_TEST(releaseText.find("padding = 0") != NomadString::npos);
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
        NomadConfigurationException,
        [](const NomadConfigurationException& error) {
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
        const auto configurationOptions = readGameOptions(configuration.document);
        BOOST_TEST(
            static_cast<int>(configuration.configuration.type) ==
            static_cast<int>(ProjectConfigurationType::Development)
        );
        BOOST_TEST(configurationOptions.entryFunction == "main");
        const auto overrides = std::string(tables).find("assets") != std::string::npos;
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
    const auto configurationOptions = readGameOptions(configuration.document);

    BOOST_TEST(!configurationOptions.title.has_value());
    BOOST_TEST(!configurationOptions.resolution.has_value());
    BOOST_TEST(!configurationOptions.size.has_value());
    BOOST_TEST(!configurationOptions.fullscreen.has_value());
    BOOST_TEST(!configurationOptions.splashImage.has_value());
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
    const auto configurationOptions = readGameOptions(configuration.document);

    BOOST_TEST(!configurationOptions.debug.has_value());
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
    const auto configurationOptions = readGameOptions(configuration.document);

    BOOST_REQUIRE(configurationOptions.resolution.has_value());
    BOOST_TEST(configurationOptions.resolution->getX() == 1280);
    BOOST_TEST(configurationOptions.resolution->getY() == 720);
    BOOST_TEST(!configurationOptions.size.has_value());
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
            BOOST_CHECK_THROW((void)readGameOptions(loadNomadProject(file).document), NomadException);
        }
    }
}

BOOST_AUTO_TEST_CASE(defaults_the_entry_function_to_main)
{
    TestDirectory directory("nomad_project_configuration_default_entry");
    auto configurationText = std::string(COMPLETE_CONFIGURATION);
    configurationText.erase(configurationText.find("entry = \"start\"\n"), 16U);
    const auto projectFile = directory.write(NOMAD_PROJECT_FILE_NAME, configurationText);

    const auto configuration = loadNomadProject(projectFile);
    const auto configurationOptions = readGameOptions(configuration.document);

    BOOST_TEST(configurationOptions.entryFunction == "main");
    const auto releaseFile = directory.write("release.toml", serializeReleaseProject(configuration));
    BOOST_TEST(readGameOptions(loadNomadProject(releaseFile).document).entryFunction == "main");
}

BOOST_AUTO_TEST_CASE(rejects_the_former_project_entry_field_with_a_migration_message)
{
    TestDirectory directory("nomad_project_configuration_former_entry");
    auto configuration = NomadConfiguration::parse(COMPLETE_CONFIGURATION);
    configuration.setString("project.entry", "start");
    const auto file = directory.write(NOMAD_PROJECT_FILE_NAME, configuration.serialize());
    BOOST_CHECK_EXCEPTION((void)loadNomadProject(file), NomadConfigurationException, [](const auto& error) {
        return NomadString(error.what()).find("has moved to 'game.entry'") != NomadString::npos;
    });
}

BOOST_AUTO_TEST_CASE(release_configuration_uses_game_entry_only)
{
    TestDirectory directory("nomad_project_configuration_game_entry");
    const auto project = loadNomadProject(directory.write(NOMAD_PROJECT_FILE_NAME, COMPLETE_CONFIGURATION));
    const auto release = NomadConfiguration::parse(serializeReleaseProject(project));
    BOOST_TEST(release.requireString("game.entry") == "start");
    BOOST_TEST(!release.contains("project.entry"));
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
        NomadConfigurationException,
        [](const NomadConfigurationException& error) {
            return std::string(error.what()).find("Unsupported project schema 2") != std::string::npos;
        }
    );

    auto missingName = std::string(COMPLETE_CONFIGURATION);
    missingName.erase(missingName.find("name = \"Test Game\"\n"), 19U);
    const auto missingNameFile = directory.write("missing-name.toml", missingName);

    BOOST_CHECK_EXCEPTION(
        (void)loadNomadProject(missingNameFile),
        NomadConfigurationException,
        [](const NomadConfigurationException& error) {
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
        NomadConfigurationException,
        [](const NomadConfigurationException& error) {
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
        NomadConfigurationException,
        [](const NomadConfigurationException& error) {
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
        NomadConfigurationException,
        [](const NomadConfigurationException& error) {
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
        NomadConfigurationException,
        [](const NomadConfigurationException& error) {
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
        NomadConfigurationException,
        [](const NomadConfigurationException& error) {
            const auto message = std::string(error.what());
            return message.find("Failed to parse project configuration at 1:") != std::string::npos;
        }
    );

    TestDirectory missingDirectory("nomad_project_configuration_missing");
    BOOST_TEST(!findProjectRoot(missingDirectory.getPath()).has_value());
    BOOST_CHECK_THROW(
        (void)discoverNomadProject(missingDirectory.getPath()),
        NomadConfigurationException
    );
}

BOOST_AUTO_TEST_SUITE_END()
