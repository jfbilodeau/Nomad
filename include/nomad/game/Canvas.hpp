// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/game/Color.hpp>
#include <nomad/geometry/Geometry.hpp>
#include <nomad/geometry/PointF.hpp>

// Forward declarations
struct SDL_Renderer;
struct SDL_Surface;

namespace nomad {

// Forward declarations
class Game;
class Rectangle;
class RectangleF;
class Sprite;
class Texture;

class Canvas {
public:
    explicit Canvas(Game* game, SDL_Renderer* renderer);
    Canvas(const Canvas& other) = delete;
    ~Canvas() = default;

    [[nodiscard]] Game* getGame() const;

    [[nodiscard]] SDL_Renderer* getSdlRenderer() const;

    void clear(const Color& color);
    void present() const;

    void setOffset(const PointF& offset);
    void setOffset(Coord x, Coord y);
    [[nodiscard]] const PointF& getOffset() const;

    void renderSprite(const Sprite* sprite, Coord x, Coord y, NomadFloat opacity = 1.0) const;
    void renderTexture(const Texture* texture, const RectangleF& source, const RectangleF& destination) const;
    void renderTexture(const Texture* texture, const RectangleF& source, const RectangleF& destination, NomadFloat opacity) const;

    void renderRectangle(const RectangleF& rectangle, const Color& color) const;

    void renderLine(Coord x1, Coord y1, Coord x2, Coord y2, const Color& color) const;
    void renderRectangleOutline(const RectangleF& rectangle, const Color& color) const;

private:
    Game* m_game;
    SDL_Renderer* m_renderer;
    PointF m_offset;
};

} // nomad
