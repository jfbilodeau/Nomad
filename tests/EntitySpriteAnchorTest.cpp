// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <TestDirectory.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Game.hpp>
#include <nomad/game/GameExecutionContext.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/game/resource/Sprite.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(entity_sprite_anchor)

BOOST_AUTO_TEST_CASE(script_setter_changes_anchor_without_moving_entity)
{
    TestDirectory directory("nomad_entity_sprite_anchor");
    GameOptions options;
    options.resourcePath = directory.getPath().string();
    auto game = Game::createHeadless(options);
    Entity entity(nullptr, game->getEntityVariables(), 1, 100.0f, 75.0f, 1);
    game->getCurrentContext()->reset(nullptr, &entity);

    auto machine = game->getRuntime()->createVirtualMachine();
    InterpreterContext interpreter(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("this.setSpriteAnchor 12.0 8.0", interpreter));

    BOOST_TEST(entity.getX() == 100.0f);
    BOOST_TEST(entity.getY() == 75.0f);
    BOOST_TEST(entity.getSpriteX() == 12.0f);
    BOOST_TEST(entity.getSpriteY() == 8.0f);
}

BOOST_AUTO_TEST_CASE(script_center_sprite_sets_half_width_and_height_offsets)
{
    TestDirectory directory("nomad_entity_center_sprite");
    GameOptions options;
    options.resourcePath = directory.getPath().string();
    auto game = Game::createHeadless(options);
    Entity entity(nullptr, game->getEntityVariables(), 1, 100.0f, 75.0f, 1);
    const Sprite sprite("test", nullptr, Rectangle{0, 0, 32, 18}, Rectangle{0, 0, 32, 18});
    entity.setSprite(&sprite);
    game->getCurrentContext()->reset(nullptr, &entity);

    auto machine = game->getRuntime()->createVirtualMachine();
    InterpreterContext interpreter(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("this.centerSprite", interpreter));

    BOOST_TEST(entity.getX() == 100.0f);
    BOOST_TEST(entity.getY() == 75.0f);
    BOOST_TEST(entity.getSpriteX() == 16.0f);
    BOOST_TEST(entity.getSpriteY() == 9.0f);
}

BOOST_AUTO_TEST_SUITE_END()
