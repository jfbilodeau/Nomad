// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/geometry/Rectangle.hpp>

#include <nomad/geometry/RectangleF.hpp>

namespace nomad {

Rectangle::Rectangle():
    m_x(0),
    m_y(0),
    m_width(0),
    m_height(0) { }

Rectangle::Rectangle(NomadInteger x, NomadInteger y, NomadInteger width, NomadInteger height):
    m_x(x),
    m_y(y),
    m_width(width),
    m_height(height) { }

Rectangle& Rectangle::set(const SDL_FRect& rect) {
    return set(
        static_cast<NomadInteger>(rect.x),
        static_cast<NomadInteger>(rect.y),
        static_cast<NomadInteger>(rect.w),
        static_cast<NomadInteger>(rect.h)
    );
}

RectangleF Rectangle::toRectangleF() const {
    return {
        static_cast<Coord>(m_x),
        static_cast<Coord>(m_y),
        static_cast<Coord>(m_width),
        static_cast<Coord>(m_height)
    };
}

SDL_Rect Rectangle::toSdlRect() const {
    return SDL_Rect{
        static_cast<int>(m_x),
        static_cast<int>(m_y),
        static_cast<int>(m_width),
        static_cast<int>(m_height)
    };
}

SDL_FRect Rectangle::toSdlFRect() const {
    return SDL_FRect{
        static_cast<float>(m_x),
        static_cast<float>(m_y),
        static_cast<float>(m_width),
        static_cast<float>(m_height)
    };
}

} // nomad
