// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/resource/ResourceManager.hpp>
#include <nomad/resource/Sprite.hpp>
#include <nomad/resource/SpritePacker.hpp>
#include <nomad/resource/Texture.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <SDL3/SDL.h>

#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

PackedSpritePage makePage(const NomadInteger width, const NomadInteger height) {
    SdlSurfacePtr surface(SDL_CreateSurface(
        static_cast<int>(width),
        static_cast<int>(height),
        SDL_PIXELFORMAT_RGBA32
    ));

    if (!surface) {
        throw std::runtime_error(SDL_GetError());
    }

    return {width, height, std::move(surface)};
}

std::unique_ptr<Game> createTestGame(const TestDirectory& directory) {
    GameOptions options;
    options.resourcePath = directory.getPath().string();
    return Game::createHeadless(std::move(options));
}

} // namespace

BOOST_AUTO_TEST_SUITE(packed_sprite_registration)

BOOST_AUTO_TEST_CASE(owns_generated_textures_and_registers_page_qualified_sprites)
{
    TestDirectory directory("nomad_packed_sprite_registration");
    auto game = createTestGame(directory);
    std::vector<PackedSpriteGroup> groups;
    PackedSpriteGroup group;
    group.name = "sprites.player";
    group.pages.push_back(makePage(8, 4));
    group.pages.push_back(makePage(3, 6));
    group.sprites.push_back({
        "sprites.player.idle",
        0,
        Rectangle(1, 1, 6, 4),
        Rectangle(2, 0, 3, 3)
    });
    group.sprites.push_back({
        "sprites.player.walk",
        1,
        Rectangle(0, 0, 3, 6),
        Rectangle(0, 0, 3, 6)
    });
    groups.push_back(std::move(group));

    game->getResources()->registerPackedSprites(groups);
    groups.clear();

    const auto* firstTexture = game->getResources()->getTextures()->getTextureByName("sprites.player#0");
    const auto* secondTexture = game->getResources()->getTextures()->getTextureByName("sprites.player#1");
    const auto* idle = game->getResources()->getSprites()->getSpriteByName("sprites.player.idle");
    const auto* walk = game->getResources()->getSprites()->getSpriteByName("sprites.player.walk");
    BOOST_REQUIRE(firstTexture != nullptr);
    BOOST_REQUIRE(secondTexture != nullptr);
    BOOST_REQUIRE(idle != nullptr);
    BOOST_REQUIRE(walk != nullptr);
    BOOST_TEST(firstTexture->getWidth() == 8);
    BOOST_TEST(firstTexture->getHeight() == 4);
    BOOST_TEST(secondTexture->getWidth() == 3);
    BOOST_TEST(secondTexture->getHeight() == 6);
    BOOST_TEST(idle->getTexture() == firstTexture);
    BOOST_TEST(walk->getTexture() == secondTexture);
}

BOOST_AUTO_TEST_CASE(rejects_a_batch_collision_without_partial_registration)
{
    TestDirectory directory("nomad_packed_sprite_registration_collision");
    auto game = createTestGame(directory);
    PackedSpriteGroup existing;
    existing.name = "sprites.player";
    existing.pages.push_back(makePage(2, 2));
    existing.sprites.push_back({
        "sprites.player.idle",
        0,
        Rectangle(0, 0, 2, 2),
        Rectangle(0, 0, 2, 2)
    });
    std::vector<PackedSpriteGroup> existingGroups;
    existingGroups.push_back(std::move(existing));
    game->getResources()->registerPackedSprites(existingGroups);

    PackedSpriteGroup unique;
    unique.name = "sprites.ui";
    unique.pages.push_back(makePage(1, 1));
    unique.sprites.push_back({
        "sprites.ui.cursor",
        0,
        Rectangle(0, 0, 1, 1),
        Rectangle(0, 0, 1, 1)
    });
    PackedSpriteGroup collision;
    collision.name = "sprites.other";
    collision.pages.push_back(makePage(1, 1));
    collision.sprites.push_back({
        "Sprites.Player.Idle",
        0,
        Rectangle(0, 0, 1, 1),
        Rectangle(0, 0, 1, 1)
    });

    std::vector<PackedSpriteGroup> collidingGroups;
    collidingGroups.push_back(std::move(unique));
    collidingGroups.push_back(std::move(collision));
    BOOST_CHECK_THROW(game->getResources()->registerPackedSprites(collidingGroups), ResourceException);
    BOOST_TEST(game->getResources()->getTextures()->getTextureByName("sprites.ui#0") == nullptr);
    BOOST_TEST(game->getResources()->getSprites()->getSpriteByName("sprites.ui.cursor") == nullptr);
    BOOST_REQUIRE(game->getResources()->getSprites()->getSpriteByName("sprites.player.idle") != nullptr);
}

BOOST_AUTO_TEST_CASE(rejects_invalid_metadata_without_registering_textures)
{
    TestDirectory directory("nomad_packed_sprite_registration_invalid");
    auto game = createTestGame(directory);
    PackedSpriteGroup group;
    group.name = "sprites.player";
    group.pages.push_back(makePage(2, 2));
    group.sprites.push_back({
        "sprites.player.invalid",
        1,
        Rectangle(0, 0, 2, 2),
        Rectangle(0, 0, 2, 2)
    });

    std::vector<PackedSpriteGroup> groups;
    groups.push_back(std::move(group));
    BOOST_CHECK_THROW(game->getResources()->registerPackedSprites(groups), ResourceException);
    BOOST_TEST(game->getResources()->getTextures()->getTextureByName("sprites.player#0") == nullptr);
    BOOST_TEST(game->getResources()->getSprites()->getSpriteByName("sprites.player.invalid") == nullptr);
}

BOOST_AUTO_TEST_CASE(rejects_frames_outside_the_page)
{
    TestDirectory directory("nomad_packed_sprite_registration_frame");
    auto game = createTestGame(directory);
    PackedSpriteGroup group;
    group.name = "sprites.player";
    group.pages.push_back(makePage(2, 2));
    group.sprites.push_back({
        "sprites.player.invalid",
        0,
        Rectangle(0, 0, 3, 2),
        Rectangle(1, 0, 2, 2)
    });

    std::vector<PackedSpriteGroup> groups;
    groups.push_back(std::move(group));
    BOOST_CHECK_THROW(game->getResources()->registerPackedSprites(groups), ResourceException);
    BOOST_TEST(game->getResources()->getTextures()->getTextureByName("sprites.player#0") == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
