// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <TestDirectory.hpp>

#include <nomad/game/Alignment.hpp>
#include <nomad/game/Canvas.hpp>
#include <nomad/game/Entity.hpp>
#include <nomad/game/Game.hpp>

#include <nomad/geometry/RectangleF.hpp>

#include <nomad/resource/Font.hpp>
#include <nomad/resource/ResourceManager.hpp>
#include <nomad/resource/Texture.hpp>

#include <SDL3/SDL.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

using namespace nomad;
using namespace nomad::test;

namespace {

struct PixelBounds {
    int left;
    int top;
    int right = -1;
    int bottom = -1;
};

PixelBounds readTextBounds(Canvas* canvas) {
    const auto surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>(
        SDL_RenderReadPixels(canvas->getSdlRenderer(), nullptr), SDL_DestroySurface);
    BOOST_REQUIRE_MESSAGE(surface, SDL_GetError());
    auto bounds = PixelBounds{surface->w, surface->h};
    for (auto y = 0; y < surface->h; ++y) {
        for (auto x = 0; x < surface->w; ++x) {
            Uint8 red, green, blue, alpha;
            if (!SDL_ReadSurfacePixel(surface.get(), x, y, &red, &green, &blue, &alpha)) {
                BOOST_FAIL(SDL_GetError());
            }
            if (red != 0 || green != 0 || blue != 0) {
                bounds.left = std::min(bounds.left, x);
                bounds.top = std::min(bounds.top, y);
                bounds.right = std::max(bounds.right, x);
                bounds.bottom = std::max(bounds.bottom, y);
            }
        }
    }
    BOOST_REQUIRE_MESSAGE(bounds.right >= 0, "Text must produce visible pixels");
    return bounds;
}

} // namespace

BOOST_AUTO_TEST_SUITE(entity_text)

BOOST_AUTO_TEST_CASE(all_anchors_render_at_expected_positions_with_single_offsets)
{
    TestDirectory directory("nomad_entity_text_alignment");
    std::filesystem::copy_file(NOMAD_TEST_FONT_FILE, directory.getPath() / "font.ttf");
    GameOptions options;
    options.resourcePath = directory.getPath().string();
    auto game = Game::createHeadless(options);
    auto* canvas = game->getCanvas();
    const auto fontId = game->getResources()->getFonts()->registerFont("font.ttf", 13.0f);
    const auto* font = game->getResources()->getFonts()->getFont(fontId);
    const auto white = Color{255, 255, 255, 255};
    const auto black = Color{0, 0, 0, 255};
    const auto texture = std::unique_ptr<Texture>(
        font->generateTexture(canvas, "Anchor", white, HorizontalAlignment::Left, 0, 0, 1.0f));
    const auto width = static_cast<Coord>(texture->getWidth());
    const auto height = static_cast<Coord>(texture->getHeight());
    canvas->clear(black);
    canvas->renderTexture(texture.get(), {0, 0, width, height}, {0, 0, width, height});
    const auto reference = readTextBounds(canvas);

    Entity entity(nullptr, game->getEntityVariables(), 1, 200.0f, 150.0f, 1);
    entity.setFontById(fontId);
    entity.setTextColor(white);
    entity.setText("Anchor");
    const auto horizontal = std::array{
        HorizontalAlignment::Left, HorizontalAlignment::Middle, HorizontalAlignment::Right};
    const auto vertical = std::array{
        VerticalAlignment::Top, VerticalAlignment::Center, VerticalAlignment::Bottom};
    const auto alignments = std::array{
        Alignment::TopLeft, Alignment::TopMiddle, Alignment::TopRight,
        Alignment::CenterLeft, Alignment::CenterMiddle, Alignment::CenterRight,
        Alignment::BottomLeft, Alignment::BottomMiddle, Alignment::BottomRight};

    for (auto row = 0U; row < vertical.size(); ++row) {
        for (auto column = 0U; column < horizontal.size(); ++column) {
            const auto alignment = alignments[row * horizontal.size() + column];
            BOOST_TEST(static_cast<int>(getAlignment(horizontal[column], vertical[row])) ==
                static_cast<int>(alignment));
            BOOST_TEST(static_cast<int>(getHorizontalAlignment(alignment)) ==
                static_cast<int>(horizontal[column]));
            BOOST_TEST(static_cast<int>(getVerticalAlignment(alignment)) ==
                static_cast<int>(vertical[row]));
            entity.setTextAlignment(alignment);
            for (const auto offset : std::array{PointF{0, 0}, PointF{17, -9}}) {
                entity.setTextPosition(offset);
                canvas->clear(black);
                entity.render(canvas);
                const auto bounds = readTextBounds(canvas);
                const auto expectedX = static_cast<int>(std::round(
                    200 + offset.getX() - width * static_cast<Coord>(column) / 2));
                const auto expectedY = static_cast<int>(std::round(
                    150 + offset.getY() - height * static_cast<Coord>(row) / 2));
                BOOST_TEST(bounds.left == expectedX + reference.left);
                BOOST_TEST(bounds.top == expectedY + reference.top);
                BOOST_TEST(bounds.right == expectedX + reference.right);
                BOOST_TEST(bounds.bottom == expectedY + reference.bottom);
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
