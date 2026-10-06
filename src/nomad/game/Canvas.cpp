// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Canvas.hpp>

#include <nomad/game/Game.hpp>
#include <nomad/geometry/RectangleF.hpp>

#include <nomad/resource/Sprite.hpp>
#include <nomad/resource/Texture.hpp>

#include <cmath>

namespace nomad {

Canvas::Canvas(Game* game, SDL_Renderer* renderer):
    m_game(game),
    m_renderer(renderer)
{
}

SDL_Renderer* Canvas::getSdlRenderer() const {
    return m_renderer;
}

Game* Canvas::getGame() const {
    return m_game;
}

void Canvas::clear(const Color& color) {
    auto result = SDL_SetRenderDrawColor(
        m_renderer,
        color.getRed(),
        color.getGreen(),
        color.getBlue(),
        color.getAlpha()
    );

    if (result == false) {
        log::error("Failed to set render draw color: " + NomadString(SDL_GetError()));
    }

    result = SDL_RenderClear(m_renderer);

    if (result == false) {
        log::error("Failed to clear renderer: " + NomadString(SDL_GetError()));
    }
}

void Canvas::present() const {
    const auto result = SDL_RenderPresent(m_renderer);

    if (result == false) {
        log::error("Failed to present renderer: " + NomadString(SDL_GetError()));
    }
}

void Canvas::setOffset(const PointF &offset) {
    m_offset = offset;
}

void Canvas::setOffset(Coord x, Coord y) {
    setOffset({x, y});
}

const PointF& Canvas::getOffset() const {
    return m_offset;
}

void Canvas::renderSprite(const Sprite* sprite, const Coord x, const Coord y, const NomadFloat opacity) const {
    if (sprite->getTexture() == nullptr) {
        log::warning("Trying to render a sprite with a null texture");

        return;
    }

    const auto frame = sprite->getFrame().toRectangleF();

    const auto& source = frame;

    const auto left = x + static_cast<Coord>(sprite->getSource().getLeft());
    const auto top = y + static_cast<Coord>(sprite->getSource().getTop());
    const auto destination = RectangleF(
        left,
        top,
        frame.getWidth(),
        frame.getHeight()
    );

    renderTexture(sprite->getTexture(), source, destination, opacity);
}

void Canvas::renderTexture(const Texture* texture, const RectangleF& source, const RectangleF& destination) const {
    auto sdlTexture = texture->getSdlTexture();

    auto sourceRect = source.toSdlFRect();

    auto destinationRect = destination.toSdlFRect();

    destinationRect.x += static_cast<float>(m_offset.getX());
    destinationRect.y += static_cast<float>(m_offset.getY());

    // Align destination rect to integer pixels to avoid sampling/bleeding gaps between tiles
    destinationRect.x = std::round(destinationRect.x);
    destinationRect.y = std::round(destinationRect.y);
    destinationRect.w = std::round(destinationRect.w);
    destinationRect.h = std::round(destinationRect.h);

    // Ensure nearest filtering for pixel-perfect tiles (prevents texture bleeding)
    SDL_SetTextureScaleMode(sdlTexture, SDL_SCALEMODE_NEAREST);

    const auto result = SDL_RenderTexture(m_renderer, sdlTexture, &sourceRect, &destinationRect);

    if (result == false) {
        log::error("Failed to render texture: " + NomadString(SDL_GetError()));
    }
}

void Canvas::renderTexture(const Texture* texture, const RectangleF& source, const RectangleF& destination, const NomadFloat opacity) const {
    const auto sdlTexture = texture->getSdlTexture();

    const auto sourceRect = source.toSdlFRect();

    auto destinationRect = destination.toSdlFRect();

    destinationRect.x += static_cast<float>(m_offset.getX());
    destinationRect.y += static_cast<float>(m_offset.getY());

    // Align destination rect to integer pixels to avoid sampling/bleeding gaps between tiles
    destinationRect.x = std::round(destinationRect.x);
    destinationRect.y = std::round(destinationRect.y);
    destinationRect.w = std::round(destinationRect.w);
    destinationRect.h = std::round(destinationRect.h);

    SDL_SetTextureScaleMode(sdlTexture, SDL_SCALEMODE_NEAREST);

    Uint8 originalAlpha;
    SDL_GetTextureAlphaMod(sdlTexture, &originalAlpha);

    const auto newAlpha = static_cast<Uint8>(opacity * 255);

    SDL_SetTextureAlphaMod(sdlTexture, newAlpha);

    const auto result = SDL_RenderTexture(m_renderer, sdlTexture, &sourceRect, &destinationRect);

    if (result == false) {
        log::error("Failed to render texture: " + NomadString(SDL_GetError()));
    }

    SDL_SetTextureAlphaMod(sdlTexture, originalAlpha);
}

void Canvas::renderRectangle(const RectangleF& rectangle, const Color& color) const {
    if (color.getAlpha() == 0) {
        return; // No need to render a transparent rectangle
    }

    SDL_BlendMode previousBlendMode;
    auto result = SDL_GetRenderDrawBlendMode(m_renderer, &previousBlendMode);

    if (result == false) {
        log::error("Failed to get blend mode: " + NomadString(SDL_GetError()));
        return;
    }

	result = SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    if (result == false) {
        log::error("Failed to set blend mode: " + NomadString(SDL_GetError()));
        return;
	}

    result = SDL_SetRenderDrawColor(
        m_renderer,
        color.getRed(),
        color.getGreen(),
        color.getBlue(),
        color.getAlpha()
    );

    if (result == false) {
        log::error("Failed to set render draw color: " + NomadString(SDL_GetError()));
        return;
    }

    auto sdlRect = rectangle.toSdlFRect();

    // Apply the offset
    sdlRect.x += static_cast<float>(m_offset.getX());
    sdlRect.y += static_cast<float>(m_offset.getY());

    result = SDL_RenderFillRect(m_renderer, &sdlRect);

    if (result == false) {
        log::error("Failed to render rectangle: " + NomadString(SDL_GetError()));
    }

    // Restore the previous blend mode)
    result = SDL_SetRenderDrawBlendMode(m_renderer, previousBlendMode);

    if (result == false) {
        log::error("Failed to restore blend mode: " + NomadString(SDL_GetError()));
    }
}
void Canvas::renderLine(
    const Coord x1,
    const Coord y1,
    const Coord x2,
    const Coord y2,
    const Color &color) const
{
    SDL_SetRenderDrawColor(m_renderer, color.getRed(), color.getGreen(), color.getBlue(), color.getAlpha());

    SDL_RenderLine(
        m_renderer,
        static_cast<float>(x1 + m_offset.getX()),
        static_cast<float>(y1 + m_offset.getY()),
        static_cast<float>(x2 + m_offset.getX()),
        static_cast<float>(y2 + m_offset.getY())
    );
}
void Canvas::renderRectangleOutline(const RectangleF &rectangle, const Color &color) const
{
    SDL_SetRenderDrawColor(m_renderer, color.getRed(), color.getGreen(), color.getBlue(), color.getAlpha());

    const SDL_FRect sdlRectangle = {
        static_cast<float>(rectangle.getLeft()),
        static_cast<float>(rectangle.getTop()),
        static_cast<float>(rectangle.getWidth()),
        static_cast<float>(rectangle.getHeight())
    };

    SDL_RenderRect(m_renderer, &sdlRectangle);
}

} // namespace nomad
