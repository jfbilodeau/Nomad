// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/game/resource/ResourceManager.hpp>
#include <nomad/game/resource/Sprite.hpp>
#include <nomad/game/resource/SpriteCatalog.hpp>
#include <nomad/game/resource/SpriteDiscovery.hpp>
#include <nomad/game/resource/SpritePacker.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/ModuleHost.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/system/ProjectSession.hpp>
#include <nomad/system/SystemFunctions.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

void writePng(const TestDirectory& directory, const NomadPath& relativePath) {
    const auto path = directory.getPath() / relativePath;
    std::filesystem::create_directories(path.parent_path());
    SdlSurfacePtr surface(SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32));

    if (!surface) {
        throw std::runtime_error(SDL_GetError());
    }
    if (!SDL_FillSurfaceRect(surface.get(), nullptr, SDL_MapSurfaceRGBA(surface.get(), 255, 0, 0, 255))) {
        throw std::runtime_error(SDL_GetError());
    }
    if (!IMG_SavePNG(surface.get(), pathToUtf8(path).c_str())) {
        throw std::runtime_error(SDL_GetError());
    }
}

GameOptions makeOptions(const TestDirectory& directory) {
    GameOptions options;
    options.resourcePath = directory.getPath().string();
    return options;
}

void writeTextCatalogue(const TestDirectory& directory) {
    directory.write("text/text.csv", "key,en\ntitle,Test\n");
    std::filesystem::create_directories(directory.getPath() / "mods");
}

void selectDummyDrivers() {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
}

std::unique_ptr<ModuleHost> createGameHost(
    const TestDirectory& directory,
    NomadConfiguration configuration = {}
) {
    configuration.setString("resources.directory", ".");
    return ModuleHost::create(
        std::move(configuration), directory.getPath(),
        resolveModules(getSdkDirectory() / "modules", {"game"}),
        {}, ModuleMode::Application
    );
}

std::unique_ptr<Game> createPreparedGame(
    Runtime& runtime,
    const GameOptions& options
) {
    registerSystemFunctions(&runtime);
    auto game = Game::createForRuntime(&runtime, options, false);
    const std::vector<NomadPath> paths{pathFromUtf8(options.resourcePath) / "scripts"};
    auto compiler = runtime.createCompiler();
    game->prepareScripts([&] {
        const auto result = compileSources(loadSourcePaths(paths), *compiler);
        if (!result.succeeded()) {
            NomadString message = "Failed to compile functions";
            for (const auto& diagnostic : result.diagnostics) {
                message += "\n" + formatDiagnostic(diagnostic);
            }
            throw GameException(message);
        }
    });
    return game;
}

} // namespace

BOOST_AUTO_TEST_SUITE(game_startup)

BOOST_AUTO_TEST_CASE(displays_the_configured_splash_at_the_configured_resolution)
{
    TestDirectory directory("nomad_game_startup_splash");
    writeTextCatalogue(directory);
    writePng(directory, "splash.png");
    directory.write("scripts/main.nomad",
        "assert window.resolution.width == 1280\nassert window.resolution.height == 720\n"
        "assert window.width == 1600\nassert window.height == 900\nsystem.exit\n");
    selectDummyDrivers();

    const auto configuration = NomadConfiguration::parse(
        "[window]\ntitle = 'Configured title'\nresolution = [1280, 720]\n"
        "size = [1600, 900]\nsplash-image = 'splash.png'\n"
    );
    const auto host = createGameHost(directory, configuration);
    BOOST_REQUIRE(host->compileProject().succeeded());
    BOOST_TEST(host->run() == 0);
}

BOOST_AUTO_TEST_CASE(uses_resolution_as_window_size_when_size_is_unconfigured)
{
    TestDirectory directory("nomad_game_startup_resolution_size");
    writeTextCatalogue(directory);
    directory.write("scripts/main.nomad",
        "assert window.resolution.width == 1024\nassert window.resolution.height == 576\n"
        "assert window.width == 1024\nassert window.height == 576\nsystem.exit\n");
    selectDummyDrivers();

    const auto host = createGameHost(directory, NomadConfiguration::parse("[window]\nresolution = [1024, 576]\n"));
    BOOST_REQUIRE(host->compileProject().succeeded());
    BOOST_TEST(host->run() == 0);
}

BOOST_AUTO_TEST_CASE(prepares_sprites_before_direct_entry_execution)
{
    TestDirectory directory("nomad_game_startup_sprites");
    writeTextCatalogue(directory);
    writePng(directory, "sprites/player.png");
    directory.write("scripts/main.nomad", "game.name = \"Initialized\"\n");
    selectDummyDrivers();

    Runtime runtime;
    auto game = createPreparedGame(runtime, makeOptions(directory));

    const auto* sprite = game->getResources()->getSprites()->getSpriteByName("sprites.player");
    BOOST_REQUIRE(sprite != nullptr);
    runtime.initializeScripts();
    game->executeEntry();
    BOOST_TEST(game->getName() == "Initialized");
}

BOOST_AUTO_TEST_CASE(loads_compiled_sprite_catalog_instead_of_raw_pngs)
{
    TestDirectory directory("nomad_game_startup_compiled_sprites");
    writeTextCatalogue(directory);
    writePng(directory, "sprites/core.png");
    const auto sources = discoverSpriteSources(directory.getPath(), {"sprites"}, true);
    const auto groups = packSpriteGroups(sources, 64, 1);
    writeSpriteCatalog(directory.getPath() / "sprites", groups);
    writePng(directory, "sprites/ignored.png");
    writePng(directory, "mod/sprites/community.png");
    directory.write("scripts/main.nomad", "game.name = \"Compiled sprites\"\n");
    selectDummyDrivers();

    auto options = makeOptions(directory);
    options.sprites.directories = std::vector<NomadPath>{"sprites", "mod/sprites"};
    Runtime runtime;
    auto game = createPreparedGame(runtime, options);

    BOOST_TEST(game->getResources()->getSprites()->getSpriteByName("sprites.core") != nullptr);
    BOOST_TEST(game->getResources()->getSprites()->getSpriteByName("sprites.ignored") == nullptr);
    BOOST_TEST(
        game->getResources()->getSprites()->getSpriteByName("mod.sprites.community") != nullptr
    );
    runtime.initializeScripts();
    game->executeEntry();
    BOOST_TEST(game->getName() == "Compiled sprites");
}

BOOST_AUTO_TEST_CASE(reports_script_compilation_failure_after_loading_valid_sprites)
{
    TestDirectory directory("nomad_game_startup_script_failure");
    writeTextCatalogue(directory);
    writePng(directory, "sprites/player.png");
    directory.write("scripts/main.nomad", "this is not valid Nomad code\n");
    selectDummyDrivers();

    const auto host = createGameHost(directory);
    const auto result = host->compileProject();
    BOOST_TEST(!result.succeeded());
    BOOST_TEST(!result.diagnostics.empty());
    BOOST_CHECK_THROW((void)host->run(), ModuleException);
}

BOOST_AUTO_TEST_CASE(reports_sprite_failure_after_successful_script_compilation)
{
    TestDirectory directory("nomad_game_startup_sprite_failure");
    writeTextCatalogue(directory);
    directory.write("sprites/player.png", "not a PNG");
    directory.write("scripts/main.nomad", "print \"compiled\"\n");
    selectDummyDrivers();

    const auto host = createGameHost(directory);
    BOOST_CHECK_EXCEPTION(
        (void)host->compileProject(),
        SpritePackerException,
        [](const SpritePackerException& error) {
            return NomadString(error.what()).find("Failed to load image for sprite") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(direct_preparation_reports_both_failures_after_joining_compilation)
{
    TestDirectory directory("nomad_game_startup_combined_failure");
    writeTextCatalogue(directory);
    directory.write("sprites/player.png", "not a PNG");
    directory.write("scripts/main.nomad", "this is not valid Nomad code\n");
    selectDummyDrivers();

    Runtime runtime;
    BOOST_CHECK_EXCEPTION(
        (void)createPreparedGame(runtime, makeOptions(directory)),
        GameException,
        [](const GameException& error) {
            const auto message = NomadString(error.what());
            return
                message.find("Sprite loading:") != NomadString::npos &&
                message.find("Failed to load image for sprite") != NomadString::npos &&
                message.find("Script compilation:") != NomadString::npos &&
                message.find("Failed to compile functions") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(executes_an_explicitly_configured_init_entry)
{
    TestDirectory directory("nomad_game_startup_custom_entry");
    writeTextCatalogue(directory);
    directory.write("scripts/init.nomad", "game.name = \"Custom entry\"\nassert game.name == \"Custom entry\"\nsystem.exit\n");
    directory.write("scripts/main.nomad", "assert false\n");
    selectDummyDrivers();

    const auto host = createGameHost(directory, NomadConfiguration::parse("[game]\nentry = 'init'\n"));
    BOOST_REQUIRE(host->compileProject().succeeded());
    BOOST_TEST(host->run() == 0);
}

BOOST_AUTO_TEST_CASE(host_initializes_hub_values_before_the_game_entry)
{
    TestDirectory directory("nomad_game_startup_hubs");
    writeTextCatalogue(directory);
    directory.write("scripts/main.nomad",
        "fun initialTitle\nreturn \"Initialized\"\nend\n"
        "hub startup\ntitle = initialTitle\nend\n"
        "assert startup.title == \"Initialized\"\ngame.name = startup.title\n"
        "assert game.name == \"Initialized\"\nsystem.exit\n");
    selectDummyDrivers();

    const auto host = createGameHost(directory);
    BOOST_REQUIRE(host->compileProject().succeeded());
    BOOST_TEST(host->run() == 0);
}

BOOST_AUTO_TEST_SUITE_END()
