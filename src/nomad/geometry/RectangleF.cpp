// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/geometry/RectangleF.hpp>

namespace nomad {

RectangleF::RectangleF():
    m_x(0),
    m_y(0),
    m_width(0),
    m_height(0) { }

RectangleF::RectangleF(Coord x, Coord y, Coord width, Coord height):
    m_x(x),
    m_y(y),
    m_width(width),
    m_height(height) { }

SDL_Rect RectangleF::toSdlRect() const {
    return SDL_Rect{
        static_cast<int>(m_x),
        static_cast<int>(m_y),
        static_cast<int>(m_width),
        static_cast<int>(m_height)
    };
}

SDL_FRect RectangleF::toSdlFRect() const {
    return SDL_FRect{
        static_cast<float>(m_x),
        static_cast<float>(m_y),
        static_cast<float>(m_width),
        static_cast<float>(m_height)
    };
}

} // nomad
