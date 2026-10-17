// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <TestDirectory.hpp>

#include <nomad/game/resource/SpriteCatalog.hpp>
#include <nomad/game/resource/SpriteDiscovery.hpp>
#include <nomad/game/resource/SpritePacker.hpp>

#include <nomad/system/Path.hpp>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <filesystem>
#include <stdexcept>

using namespace nomad;
using namespace nomad::test;

namespace {

void writePng(
    const TestDirectory& directory,
    const NomadPath& relativePath,
    const Uint8 red,
    const Uint8 green
) {
    const auto path = directory.getPath() / relativePath;
    std::filesystem::create_directories(path.parent_path());
    SdlSurfacePtr surface(SDL_CreateSurface(3, 2, SDL_PIXELFORMAT_RGBA32));

    if (!surface) {
        throw std::runtime_error(SDL_GetError());
    }
    if (!SDL_FillSurfaceRect(
        surface.get(),
        nullptr,
        SDL_MapSurfaceRGBA(surface.get(), red, green, 0, 255)
    )) {
        throw std::runtime_error(SDL_GetError());
    }
    if (!IMG_SavePNG(surface.get(), pathToUtf8(path).c_str())) {
        throw std::runtime_error(SDL_GetError());
    }
}

const PackedSpriteDefinition* findSprite(
    const std::vector<PackedSpriteGroup>& groups,
    const NomadStringView name
) {
    for (const auto& group : groups) {
        const auto sprite = std::ranges::find(group.sprites, name, &PackedSpriteDefinition::name);

        if (sprite != group.sprites.end()) {
            return &*sprite;
        }
    }

    return nullptr;
}

} // namespace

BOOST_AUTO_TEST_SUITE(sprite_catalog)

BOOST_AUTO_TEST_CASE(round_trip_preserves_duplicate_sprite_aliases)
{
    TestDirectory directory("nomad_sprite_catalog_round_trip");
    writePng(directory, "source/sprites/idle.00.png", 255, 0);
    writePng(directory, "source/sprites/idle.01.png", 255, 0);
    const auto sources = discoverSpriteSources(
        directory.getPath() / "source",
        {"sprites"},
        true
    );
    const auto packed = packSpriteGroups(sources, 64, 1);
    const auto compiledRoot = directory.getPath() / "compiled/sprites";

    writeSpriteCatalog(compiledRoot, packed);
    const auto loaded = loadSpriteCatalog(compiledRoot);
    const auto* first = findSprite(loaded, "sprites.idle.00");
    const auto* second = findSprite(loaded, "sprites.idle.01");

    BOOST_REQUIRE(first != nullptr);
    BOOST_REQUIRE(second != nullptr);
    BOOST_TEST(first->page == second->page);
    BOOST_TEST(first->frame.getLeft() == second->frame.getLeft());
    BOOST_TEST(first->frame.getTop() == second->frame.getTop());
    BOOST_TEST(first->frame.getWidth() == second->frame.getWidth());
    BOOST_TEST(first->frame.getHeight() == second->frame.getHeight());
    BOOST_TEST(std::filesystem::is_regular_file(compiledRoot / NOMAD_SPRITE_CATALOG_FILE_NAME));
    BOOST_TEST(std::filesystem::is_regular_file(
        compiledRoot / makePackedSpritePageFileName("sprites", 0)
    ));
}

BOOST_AUTO_TEST_CASE(selects_compiled_or_raw_loading_per_root)
{
    TestDirectory directory("nomad_sprite_catalog_mixed_roots");
    writePng(directory, "sprites/core.png", 255, 0);
    const auto coreSources = discoverSpriteSources(directory.getPath(), {"sprites"}, true);
    const auto coreGroups = packSpriteGroups(coreSources, 64, 1);
    writeSpriteCatalog(directory.getPath() / "sprites", coreGroups);
    writePng(directory, "sprites/ignored.png", 0, 255);
    writePng(directory, "mod/sprites/community.png", 0, 255);

    const auto core = loadSpriteRoot(
        directory.getPath(),
        "sprites",
        true,
        64,
        1
    );
    const auto mod = loadSpriteRoot(directory.getPath(), "mod/sprites", true, 64, 1);

    BOOST_TEST(findSprite(core, "sprites.core") != nullptr);
    BOOST_TEST(findSprite(core, "sprites.ignored") == nullptr);
    BOOST_TEST(findSprite(mod, "mod.sprites.community") != nullptr);
}

BOOST_AUTO_TEST_CASE(malformed_catalog_does_not_fall_back_to_raw_images)
{
    TestDirectory directory("nomad_sprite_catalog_invalid");
    writePng(directory, "sprites/core.png", 255, 0);
    directory.write("sprites/catalog.json", "{\"schema\":1,\"groups\":\"invalid\"}");

    BOOST_CHECK_EXCEPTION(
        (void)loadSpriteRoot(directory.getPath(), "sprites", true, 64, 1),
        SpriteCatalogException,
        [](const SpriteCatalogException& error) {
            return NomadString(error.what()).find("field 'groups' must be an array") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(packaged_projects_allow_missing_mod_roots)
{
    TestDirectory directory("nomad_sprite_catalog_missing_mod_root");

    const auto groups = loadSpriteRoot(directory.getPath(), "mod/sprites", false, 64, 1);

    BOOST_TEST(groups.empty());
    BOOST_CHECK_THROW(
        (void)loadSpriteRoot(directory.getPath(), "mod/sprites", true, 64, 1),
        SpriteCatalogException
    );
}

BOOST_AUTO_TEST_SUITE_END()
