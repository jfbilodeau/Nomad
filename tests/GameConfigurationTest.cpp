// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/GameConfiguration.hpp>

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/system/ModuleHost.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <SDL3/SDL.h>

using namespace nomad;
using namespace nomad::test;

namespace {

ModulePackageContext packageContext(
    const NomadConfiguration& source,
    NomadConfiguration& release,
    const TestDirectory& directory,
    const bool dryRun
) {
    ModulePackageContext context{};
    context.source = &source;
    context.release = &release;
    context.projectDirectory = directory.getPath();
    context.resourceDirectory = directory.getPath();
    context.stagingDirectory = directory.getPath() / "dist" / "staging";
    context.dryRun = dryRun;
    context.recordFile = [](const NomadPath&) { BOOST_FAIL("No resources should be generated"); };
    context.excludeResource = [](const NomadPath&) { BOOST_FAIL("No resources should be excluded"); };
    context.isResourceExcluded = [](const NomadPath&) { return false; };
    context.copyDirectory = [](const NomadPath& source, const NomadPath& destination) {
        BOOST_TEST(source.filename() == "res");
        BOOST_TEST(destination == NomadPath("res"));
    };
    return context;
}

} // namespace

BOOST_AUTO_TEST_SUITE(project_game_configuration)

BOOST_AUTO_TEST_CASE(game_resource_directories_are_normalized_without_project_or_script_configuration)
{
    for (const auto section : {"audio", "sprites"}) {
        const auto source = NomadConfiguration::parse(
            "[" + NomadString(section) + "]\ndirectories = ['effects/nested/..', 'mod\\effects']\n"
            "[resources]\ndirectory = 'assets'\n"
        );
        const auto options = readGameOptions(source);
        const auto& directories = NomadStringView(section) == "audio"
            ? options.audio.directories : options.sprites.directories;
        BOOST_REQUIRE(directories.has_value());
        BOOST_REQUIRE(directories->size() == 2U);
        BOOST_TEST((*directories)[0] == NomadPath("effects"));
        BOOST_TEST((*directories)[1] == NomadPath("mod/effects"));
        auto release = source;
        normalizeGameConfiguration(source, release);
        const auto normalized = release.requireStringArray(NomadString(section) + ".directories");
        BOOST_REQUIRE(normalized.size() == 2U);
        BOOST_TEST(normalized[0] == "effects");
        BOOST_TEST(normalized[1] == "mod/effects");
        BOOST_TEST(!release.contains("scripts"));
    }
}

BOOST_AUTO_TEST_CASE(game_rejects_invalid_resource_directory_lists)
{
    for (const auto section : {"audio", "sprites"}) {
        const auto field = NomadString(section) + ".directories";
        for (const auto value : {
            "false", "['effects', 42]", "['']", "['.']", "['/effects']", "['../effects']",
            "['effects', 'effects/../effects']", "['effects', 'effects/nested']",
            "['effects/nested', 'effects']", "['Effects', 'effects']"
        }) {
            const auto source = NomadConfiguration::parse(
                "[" + NomadString(section) + "]\ndirectories = " + value
            );
            BOOST_TEST_CONTEXT(field << " = " << value) {
                BOOST_CHECK_EXCEPTION(
                    (void)readGameOptions(source),
                    NomadConfigurationException,
                    [&field](const auto& error) {
                        return NomadStringView(error.what()).find(field) != NomadStringView::npos;
                    }
                );
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(packaging_delegates_game_configuration_without_creating_a_runtime_or_initializing_sdl)
{
    const TestDirectory directory("nomad_game_configuration_packaging");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-game"});
    const auto source = NomadConfiguration::parse(
        "[game]\nentry = \"launch\"\ndebug = false\ncustom = \"preserved\"\n"
        "[window]\nresolution = [1280, 720]\nsplash-image = 'images\\menu\\..\\splash.png'\n"
        "[audio]\ndirectories = ['audio\\effects\\..']\n"
        "[sprites]\ndirectories = []\npadding = 0\n"
        "[text]\ndirectories = ['text\\en.csv']\n"
        "[custom-module]\nsetting = \"untouched\"\n"
    );
    const auto original = source.serialize();
    const auto initializedSubsystems = SDL_WasInit(0);
    for (const auto dryRun : {true, false}) {
        auto host = ModuleHost::create(source, directory.getPath(), modules, {}, ModuleMode::Packaging);
        BOOST_TEST(host->getRuntime() == nullptr);
        auto release = source;
        auto context = packageContext(source, release, directory, dryRun);
        host->package(context);

        BOOST_TEST(release.requireString("game.entry") == "launch");
        BOOST_TEST(!release.requireBoolean("game.debug"));
        BOOST_TEST(release.requireString("resources.directory") == "res");
        BOOST_TEST(release.requireString("game.custom") == "preserved");
        BOOST_TEST(release.requireString("custom-module.setting") == "untouched");
        BOOST_TEST(release.requireString("window.splash-image") == "images/splash.png");
        const auto audioDirectories = release.requireStringArray("audio.directories");
        BOOST_REQUIRE(audioDirectories.size() == 1U);
        BOOST_TEST(audioDirectories.front() == "audio");
        const auto textFiles = release.requireStringArray("text.directories");
        BOOST_REQUIRE(textFiles.size() == 1U);
        BOOST_TEST(textFiles.front() == "text/en.csv");
        const auto options = readGameOptions(release);
        BOOST_REQUIRE(options.resolution.has_value());
        BOOST_TEST(options.resolution->getX() == 1280);
        BOOST_TEST(options.resolution->getY() == 720);
        BOOST_REQUIRE(options.sprites.padding.has_value());
        BOOST_TEST(*options.sprites.padding == 0);
        BOOST_TEST(source.serialize() == original);
        BOOST_TEST(SDL_WasInit(0) == initializedSubsystems);
        BOOST_TEST(!std::filesystem::exists(context.stagingDirectory));
    }
}

BOOST_AUTO_TEST_CASE(game_validation_is_shared_by_application_analysis_and_packaging)
{
    const TestDirectory directory("nomad_game_configuration_invalid");
    const auto modules = resolveModules(getSdkDirectory() / "modules", {"nomad-game"});
    for (const auto& text : {
        "[game]\nentry = \"\"\n",
        "[game]\ndebug = \"yes\"\n",
        "[window]\nresolution = [640, 0]\n",
        "[window]\nsplash-image = \"../splash.png\"\n",
        "[audio]\ndirectories = [\"audio\", \"audio/effects\"]\n",
        "[sprites]\npadding = -1\n",
        "[text]\ndirectories = [\"text/en.txt\"]\n",
        "[resources]\ndirectory = \"../assets\"\n",
        "[resources]\ndirectory = '/assets'\n",
        "window = false\n",
        "game = []\n",
        "[window]\nresolution = [640, 480.0]\n",
        "[window]\nsize = [true, 480]\n"
    }) {
        const auto source = NomadConfiguration::parse(text);
        BOOST_TEST_CONTEXT(text) {
            for (const auto mode : {ModuleMode::Application, ModuleMode::Analysis}) {
                BOOST_CHECK_THROW(
                    (void)ModuleHost::create(source, directory.getPath(), modules, {}, mode),
                    NomadException
                );
            }
            for (const auto dryRun : {true, false}) {
                auto host = ModuleHost::create(source, directory.getPath(), modules, {}, ModuleMode::Packaging);
                auto release = source;
                auto context = packageContext(source, release, directory, dryRun);
                BOOST_CHECK_THROW(host->package(context), NomadException);
                BOOST_TEST(!std::filesystem::exists(context.stagingDirectory));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(api_only_projects_preserve_game_configuration_without_validating_it)
{
    const TestDirectory directory("nomad_configuration_unselected_game");
    const auto file = directory.write(NOMAD_PROJECT_FILE_NAME,
        "schema = 1\n[project]\nname = \"API\"\nidentifier = \"org.nomad.api\"\n"
        "version = \"0.1.0\"\nexecutable = \"api\"\n"
        "[nomad]\nversion = \"0.1.0\"\nmodules = [\"nomad-system\"]\n"
        "[game]\nentry = 42\n[window]\nresolution = false\n[audio]\ndirectories = 42\n"
    );
    const auto project = loadNomadProject(file);
    const auto modules = resolveModules(getSdkDirectory() / "modules", project.configuration.sdk.modules);
    auto release = NomadConfiguration::parse(serializeReleaseProject(project));
    for (const auto mode : {ModuleMode::Application, ModuleMode::Analysis, ModuleMode::Packaging}) {
        auto host = ModuleHost::create(project.document, project.root, modules, {}, mode);
        if (mode == ModuleMode::Packaging) {
            auto context = packageContext(project.document, release, directory, true);
            host->package(context);
        }
    }
    BOOST_TEST(release.requireInteger("game.entry") == 42);
    BOOST_TEST(!release.requireBoolean("window.resolution"));
    BOOST_TEST(release.requireInteger("audio.directories") == 42);
}

BOOST_AUTO_TEST_CASE(game_configuration_does_not_interpret_or_normalize_script_settings)
{
    for (const auto text : {
        "scripts = false\n",
        "[scripts]\ndirectories = false\n",
        "[scripts]\ndirectories = ['assets/scripts']\n"
    }) {
        auto source = NomadConfiguration::parse(text);
        source.setString("resources.directory", "assets");
        auto release = source;
        BOOST_CHECK_NO_THROW((void)readGameOptions(source));
        normalizeGameConfiguration(source, release);
        BOOST_TEST(release.requireString("resources.directory") == "res");
        release.erase("resources.directory");
        source.erase("resources.directory");
        BOOST_TEST(release.serialize() == source.serialize());
    }
}

BOOST_AUTO_TEST_SUITE_END()
