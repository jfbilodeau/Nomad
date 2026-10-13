// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/SpriteDiscovery.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <system_error>
#include <vector>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(sprite_discovery)

BOOST_AUTO_TEST_CASE(discovers_recursive_png_files_and_derives_names)
{
    TestDirectory directory("nomad_sprite_discovery_names");
    directory.write("res/sprites/logo.png", "");
    directory.write("res/sprites/player/idle/west-00.png", "");
    directory.write("res/sprites/player/move/East.PNG", "");
    directory.write("res/sprites/player/notes.txt", "");
    directory.write("res/mod/sprites/monsters/slime/idle-00.png", "");

    const auto sources = discoverSpriteSources(
        directory.getPath() / "res",
        {NomadPath("sprites"), NomadPath("mod/sprites")},
        true
    );

    BOOST_REQUIRE(sources.size() == 4U);
    BOOST_TEST(sources[0].name == "mod.sprites.monsters.slime.idle-00");
    BOOST_TEST(sources[0].group == "mod.sprites.monsters");
    BOOST_TEST(sources[1].name == "sprites.logo");
    BOOST_TEST(sources[1].group == "sprites");
    BOOST_TEST(sources[2].name == "sprites.player.idle.west-00");
    BOOST_TEST(sources[2].group == "sprites.player");
    BOOST_TEST(sources[3].name == "sprites.player.move.East");
    BOOST_TEST(sources[3].group == "sprites.player");
}

BOOST_AUTO_TEST_CASE(orders_sources_deterministically)
{
    TestDirectory directory("nomad_sprite_discovery_order");
    directory.write("res/sprites/zeta.png", "");
    directory.write("res/sprites/Alpha.png", "");
    directory.write("res/sprites/middle.png", "");

    const auto sources = discoverSpriteSources(
        directory.getPath() / "res",
        {NomadPath("sprites")},
        true
    );

    BOOST_REQUIRE(sources.size() == 3U);
    BOOST_TEST(sources[0].name == "sprites.Alpha");
    BOOST_TEST(sources[1].name == "sprites.middle");
    BOOST_TEST(sources[2].name == "sprites.zeta");
}

BOOST_AUTO_TEST_CASE(allows_a_missing_implicit_default_directory)
{
    TestDirectory directory("nomad_sprite_discovery_default_missing");

    const auto sources = discoverSpriteSources(
        directory.getPath() / "res",
        {NomadPath("sprites")},
        false
    );

    BOOST_TEST(sources.empty());
}

BOOST_AUTO_TEST_CASE(rejects_a_missing_explicit_directory)
{
    TestDirectory directory("nomad_sprite_discovery_explicit_missing");

    BOOST_CHECK_EXCEPTION(
        (void)discoverSpriteSources(
            directory.getPath() / "res",
            {NomadPath("sprites")},
            true
        ),
        SpriteDiscoveryError,
        [](const SpriteDiscoveryError& error) {
            return NomadString(error.what()).find("does not exist") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(rejects_case_only_name_collisions)
{
    TestDirectory directory("nomad_sprite_discovery_collision");
    directory.write("res/sprites/player.walk.png", "");
    directory.write("res/sprites/player/Walk.png", "");

    BOOST_CHECK_EXCEPTION(
        (void)discoverSpriteSources(
            directory.getPath() / "res",
            {NomadPath("sprites")},
            true
        ),
        SpriteDiscoveryError,
        [](const SpriteDiscoveryError& error) {
            return NomadString(error.what()).find("name collision") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(rejects_paths_outside_the_resource_directory)
{
    TestDirectory directory("nomad_sprite_discovery_escape");

    BOOST_CHECK_THROW(
        (void)discoverSpriteSources(
            directory.getPath() / "res",
            {NomadPath("../sprites")},
            true
        ),
        SpriteDiscoveryError
    );
}

BOOST_AUTO_TEST_CASE(rejects_symbolic_links_when_supported)
{
    TestDirectory directory("nomad_sprite_discovery_symlink");
    directory.write("outside/sprite.png", "");
    std::filesystem::create_directories(directory.getPath() / "res/sprites");

    std::error_code error;
    std::filesystem::create_symlink(
        directory.getPath() / "outside/sprite.png",
        directory.getPath() / "res/sprites/link.png",
        error
    );

    if (error) {
        BOOST_TEST_MESSAGE("Symbolic link creation is unavailable: " << error.message());
        return;
    }

    BOOST_CHECK_EXCEPTION(
        (void)discoverSpriteSources(
            directory.getPath() / "res",
            {NomadPath("sprites")},
            true
        ),
        SpriteDiscoveryError,
        [](const SpriteDiscoveryError& exception) {
            return NomadString(exception.what()).find("symbolic links") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_SUITE_END()
