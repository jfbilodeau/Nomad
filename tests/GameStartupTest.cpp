// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/resource/ResourceManager.hpp>
#include <nomad/resource/Sprite.hpp>
#include <nomad/resource/SpriteCatalog.hpp>
#include <nomad/resource/SpriteDiscovery.hpp>
#include <nomad/resource/SpritePacker.hpp>

#include <nomad/system/Path.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <filesystem>
#include <stdexcept>
#include <utility>

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

void createGameDiscardingResult(const TestDirectory& directory) {
    auto game = Game::create(makeOptions(directory));
}

} // namespace

BOOST_AUTO_TEST_SUITE(game_startup)

BOOST_AUTO_TEST_CASE(loads_sprites_before_executing_the_entry_function)
{
    TestDirectory directory("nomad_game_startup_sprites");
    writeTextCatalogue(directory);
    writePng(directory, "sprites/player.png");
    directory.write("scripts/init.nomad", "game.name = \"Initialized\"\n");
    selectDummyDrivers();

    auto game = Game::create(makeOptions(directory));

    const auto* sprite = game->getResources()->getSprites()->getSpriteByName("sprites.player");
    BOOST_REQUIRE(sprite != nullptr);
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
    directory.write("scripts/init.nomad", "game.name = \"Compiled sprites\"\n");
    selectDummyDrivers();

    auto options = makeOptions(directory);
    options.sprites.directories = {"sprites", "mod/sprites"};
    options.sprites.directoriesExplicitlyConfigured = true;
    auto game = Game::create(std::move(options));

    BOOST_TEST(game->getResources()->getSprites()->getSpriteByName("sprites.core") != nullptr);
    BOOST_TEST(game->getResources()->getSprites()->getSpriteByName("sprites.ignored") == nullptr);
    BOOST_TEST(
        game->getResources()->getSprites()->getSpriteByName("mod.sprites.community") != nullptr
    );
    BOOST_TEST(game->getName() == "Compiled sprites");
}

BOOST_AUTO_TEST_CASE(reports_script_compilation_failure_after_loading_valid_sprites)
{
    TestDirectory directory("nomad_game_startup_script_failure");
    writeTextCatalogue(directory);
    writePng(directory, "sprites/player.png");
    directory.write("scripts/init.nomad", "this is not valid Nomad code\n");
    selectDummyDrivers();

    BOOST_CHECK_EXCEPTION(
        createGameDiscardingResult(directory),
        GameException,
        [](const GameException& error) {
            return NomadString(error.what()).find("Failed to compile functions") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(reports_sprite_failure_after_successful_script_compilation)
{
    TestDirectory directory("nomad_game_startup_sprite_failure");
    writeTextCatalogue(directory);
    directory.write("sprites/player.png", "not a PNG");
    directory.write("scripts/init.nomad", "log.info \"compiled\"\n");
    selectDummyDrivers();

    BOOST_CHECK_EXCEPTION(
        createGameDiscardingResult(directory),
        SpritePackerError,
        [](const SpritePackerError& error) {
            return NomadString(error.what()).find("Failed to load image for sprite") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(reports_both_failures_after_joining_compilation)
{
    TestDirectory directory("nomad_game_startup_combined_failure");
    writeTextCatalogue(directory);
    directory.write("sprites/player.png", "not a PNG");
    directory.write("scripts/init.nomad", "this is not valid Nomad code\n");
    selectDummyDrivers();

    BOOST_CHECK_EXCEPTION(
        createGameDiscardingResult(directory),
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

BOOST_AUTO_TEST_SUITE_END()
