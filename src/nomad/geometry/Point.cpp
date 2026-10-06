// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/geometry/Point.hpp>

#include <nomad/geometry/PointF.hpp>

namespace nomad {

Point::Point():
    m_x(0),
    m_y(0) { }

Point::Point(const NomadInteger x, const NomadInteger y):
    m_x(x),
    m_y(y) { }

PointF Point::toPointF() const {
    return {
        static_cast<Coord>(m_x),
        static_cast<Coord>(m_y)
    };
}

PointF & Point::toPointF(PointF &point) const {
    point.set(
        static_cast<Coord>(m_x),
        static_cast<Coord>(m_y)
    );

    return point;
}

SDL_Point Point::toSdlPoint() const {
    return SDL_Point{
        static_cast<int>(m_x),
        static_cast<int>(m_y)
    };
}

SDL_Point& Point::toSdlPoint(SDL_Point& point) const {
    point.x = static_cast<int>(m_x);
    point.y = static_cast<int>(m_y);

    return point;
}

SDL_FPoint Point::toSdlFPoint() const {
    return SDL_FPoint{
        static_cast<float>(m_x),
        static_cast<float>(m_y)
    };
}

SDL_FPoint& Point::toSdlFPoint(SDL_FPoint& point) const {
    point.x = static_cast<float>(m_x);
    point.y = static_cast<float>(m_y);

    return point;
}

} // nomad
