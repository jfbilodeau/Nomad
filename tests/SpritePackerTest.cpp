// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/SpritePacker.hpp>

#include <nomad/system/Path.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <filesystem>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

struct TestPixel {
    int x;
    int y;
    Uint8 red;
    Uint8 green;
    Uint8 blue;
    Uint8 alpha;
};

NomadPath writePng(
    const TestDirectory& directory,
    const NomadPath& relativePath,
    const int width,
    const int height,
    const std::vector<TestPixel>& pixels
) {
    const auto path = directory.getPath() / relativePath;
    std::filesystem::create_directories(path.parent_path());
    SdlSurfacePtr surface(SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32));

    if (!surface) {
        throw std::runtime_error(SDL_GetError());
    }
    if (!SDL_FillSurfaceRect(surface.get(), nullptr, SDL_MapSurfaceRGBA(surface.get(), 0, 0, 0, 0))) {
        throw std::runtime_error(SDL_GetError());
    }

    for (const auto& pixel : pixels) {
        if (!SDL_WriteSurfacePixel(
            surface.get(),
            pixel.x,
            pixel.y,
            pixel.red,
            pixel.green,
            pixel.blue,
            pixel.alpha
        )) {
            throw std::runtime_error(SDL_GetError());
        }
    }

    if (!IMG_SavePNG(surface.get(), pathToUtf8(path).c_str())) {
        throw std::runtime_error(SDL_GetError());
    }

    return path;
}

void checkPixel(
    SDL_Surface* surface,
    const int x,
    const int y,
    const Uint8 expectedRed,
    const Uint8 expectedGreen,
    const Uint8 expectedBlue,
    const Uint8 expectedAlpha
) {
    Uint8 red = 0;
    Uint8 green = 0;
    Uint8 blue = 0;
    Uint8 alpha = 0;
    BOOST_REQUIRE(SDL_ReadSurfacePixel(surface, x, y, &red, &green, &blue, &alpha));
    BOOST_TEST(red == expectedRed);
    BOOST_TEST(green == expectedGreen);
    BOOST_TEST(blue == expectedBlue);
    BOOST_TEST(alpha == expectedAlpha);
}

} // namespace

BOOST_AUTO_TEST_SUITE(sprite_packer)

BOOST_AUTO_TEST_CASE(trims_images_and_preserves_logical_dimensions)
{
    TestDirectory directory("nomad_sprite_packer_trim");
    const auto path = writePng(
        directory,
        "sprites/player.png",
        4,
        4,
        {
            {1, 1, 255, 0, 0, 255},
            {2, 2, 0, 255, 0, 255}
        }
    );
    const auto groups = packSpriteGroups(
        {{path, "sprites.player", "sprites"}},
        16,
        1
    );

    BOOST_REQUIRE(groups.size() == 1U);
    BOOST_REQUIRE(groups[0].pages.size() == 1U);
    BOOST_REQUIRE(groups[0].sprites.size() == 1U);
    const auto& sprite = groups[0].sprites[0];
    BOOST_TEST(groups[0].name == "sprites");
    BOOST_TEST(groups[0].pages[0].width == 2);
    BOOST_TEST(groups[0].pages[0].height == 2);
    BOOST_TEST(sprite.source.getLeft() == 1);
    BOOST_TEST(sprite.source.getTop() == 1);
    BOOST_TEST(sprite.source.getWidth() == 4);
    BOOST_TEST(sprite.source.getHeight() == 4);
    BOOST_TEST(sprite.frame.getWidth() == 2);
    BOOST_TEST(sprite.frame.getHeight() == 2);
    checkPixel(groups[0].pages[0].surface.get(), 0, 0, 255, 0, 0, 255);
    checkPixel(groups[0].pages[0].surface.get(), 1, 1, 0, 255, 0, 255);
}

BOOST_AUTO_TEST_CASE(aliases_exact_trimmed_images)
{
    TestDirectory directory("nomad_sprite_packer_alias");
    const auto first = writePng(
        directory,
        "sprites/first.png",
        4,
        4,
        {{1, 1, 10, 20, 30, 255}}
    );
    const auto second = writePng(
        directory,
        "sprites/second.png",
        6,
        5,
        {{3, 2, 10, 20, 30, 255}}
    );
    const auto different = writePng(
        directory,
        "sprites/different.png",
        4,
        4,
        {{1, 1, 30, 20, 10, 255}}
    );
    const auto groups = packSpriteGroups(
        {
            {first, "sprites.first", "sprites"},
            {second, "sprites.second", "sprites"},
            {different, "sprites.different", "sprites"}
        },
        16,
        1
    );

    BOOST_REQUIRE(groups.size() == 1U);
    BOOST_REQUIRE(groups[0].sprites.size() == 3U);
    const auto& firstSprite = groups[0].sprites[0];
    const auto& secondSprite = groups[0].sprites[1];
    const auto& differentSprite = groups[0].sprites[2];
    BOOST_TEST(firstSprite.page == secondSprite.page);
    BOOST_TEST(firstSprite.frame.getLeft() == secondSprite.frame.getLeft());
    BOOST_TEST(firstSprite.frame.getTop() == secondSprite.frame.getTop());
    BOOST_TEST(firstSprite.source.getLeft() == 1);
    BOOST_TEST(secondSprite.source.getLeft() == 3);
    const auto differentFrame =
        firstSprite.frame.getLeft() != differentSprite.frame.getLeft() ||
        firstSprite.frame.getTop() != differentSprite.frame.getTop();
    BOOST_TEST(differentFrame);
}

BOOST_AUTO_TEST_CASE(aliases_fully_transparent_images_to_one_pixel)
{
    TestDirectory directory("nomad_sprite_packer_transparent");
    const auto first = writePng(directory, "sprites/first.png", 4, 3, {});
    const auto second = writePng(directory, "sprites/second.png", 8, 6, {});
    const auto groups = packSpriteGroups(
        {
            {first, "sprites.first", "sprites"},
            {second, "sprites.second", "sprites"}
        },
        16,
        1
    );

    BOOST_REQUIRE(groups.size() == 1U);
    BOOST_REQUIRE(groups[0].pages.size() == 1U);
    BOOST_REQUIRE(groups[0].sprites.size() == 2U);
    BOOST_TEST(groups[0].pages[0].width == 1);
    BOOST_TEST(groups[0].pages[0].height == 1);
    BOOST_TEST(groups[0].sprites[0].source.getWidth() == 4);
    BOOST_TEST(groups[0].sprites[0].source.getHeight() == 3);
    BOOST_TEST(groups[0].sprites[1].source.getWidth() == 8);
    BOOST_TEST(groups[0].sprites[1].source.getHeight() == 6);
    BOOST_TEST(groups[0].sprites[0].frame.getLeft() == groups[0].sprites[1].frame.getLeft());
    BOOST_TEST(groups[0].sprites[0].frame.getTop() == groups[0].sprites[1].frame.getTop());
    checkPixel(groups[0].pages[0].surface.get(), 0, 0, 0, 0, 0, 0);
}

BOOST_AUTO_TEST_CASE(separates_groups_and_splits_physical_pages)
{
    TestDirectory directory("nomad_sprite_packer_groups");
    const auto red = writePng(directory, "sprites/player/red.png", 4, 4, {{0, 0, 255, 0, 0, 255}});
    const auto green = writePng(directory, "sprites/player/green.png", 4, 4, {{0, 0, 0, 255, 0, 255}});
    const auto blue = writePng(directory, "sprites/ui/blue.png", 4, 4, {{0, 0, 0, 0, 255, 255}});
    const auto groups = packSpriteGroups(
        {
            {red, "sprites.player.red", "sprites.player"},
            {green, "sprites.player.green", "sprites.player"},
            {blue, "sprites.ui.blue", "sprites.ui"}
        },
        1,
        1
    );

    BOOST_REQUIRE(groups.size() == 2U);
    BOOST_TEST(groups[0].name == "sprites.player");
    BOOST_TEST(groups[1].name == "sprites.ui");
    BOOST_REQUIRE(groups[0].pages.size() == 2U);
    BOOST_REQUIRE(groups[1].pages.size() == 1U);
}

BOOST_AUTO_TEST_CASE(reports_invalid_image_sources)
{
    TestDirectory directory("nomad_sprite_packer_invalid");
    const auto path = directory.write("sprites/invalid.png", "not a PNG");

    BOOST_CHECK_EXCEPTION(
        (void)packSpriteGroups(
            {{path, "sprites.invalid", "sprites"}},
            16,
            1
        ),
        SpritePackerException,
        [](const SpritePackerException& error) {
            const auto message = NomadString(error.what());
            return
                message.find("sprites.invalid") != NomadString::npos &&
                message.find("invalid.png") != NomadString::npos;
        }
    );
}

BOOST_AUTO_TEST_CASE(reports_physical_pages_aliases_and_used_pixels)
{
    TestDirectory directory("nomad_sprite_packing_report");
    writePng(directory, "sprites/player/first.png", 4, 4, {{1, 1, 10, 20, 30, 255}});
    writePng(directory, "sprites/player/alias.png", 6, 5, {{3, 2, 10, 20, 30, 255}});
    writePng(directory, "sprites/player/different.png", 4, 4, {{1, 1, 30, 20, 10, 255}});

    const auto report = analyzeSpritePacking(
        directory.getPath(),
        {"sprites"},
        true,
        16,
        1
    );

    BOOST_TEST(report.sourceCount == 3U);
    BOOST_TEST(report.groupCount == 1U);
    BOOST_TEST(report.pageCount == 1U);
    BOOST_TEST(report.uniqueSpriteCount == 2U);
    BOOST_REQUIRE(report.pages.size() == 1U);
    BOOST_TEST(report.pages[0].name == "sprites.player#0");
    BOOST_TEST(report.pages[0].spriteCount == 3U);
    BOOST_TEST(report.pages[0].uniqueSpriteCount == 2U);
    BOOST_TEST(report.pages[0].usedPixelCount == 2U);
}

BOOST_AUTO_TEST_SUITE_END()
