// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <SDL3/SDL_rect.h>

namespace nomad {

class PointF;

class Point {
public:
    Point();
    Point(NomadInteger x, NomadInteger y);
    Point(const Point& other) = default;
    Point(Point&& other) noexcept = default;

    Point& operator=(const Point& other) = default;
    Point& operator=(Point&& other) noexcept = default;

    [[nodiscard]] NomadInteger getX() const {
        return m_x;
    }

    [[nodiscard]] NomadInteger getY() const {
        return m_y;
    }

    void setX(NomadInteger x) {
        m_x = x;
    }

    void setY(NomadInteger y) {
        m_y = y;
    }

    void set(const NomadInteger x, const NomadInteger y) {
        m_x = x;
        m_y = y;
    }

    void set(const Point& other) {
        m_x = other.m_x;
        m_y = other.m_y;
    }

    void move(const NomadInteger dx, const NomadInteger dy) {
        m_x += dx;
        m_y += dy;
    }

    void move(const Point& delta) {
        m_x += delta.m_x;
        m_y += delta.m_y;
    }

    [[nodiscard]] PointF toPointF() const;
    PointF& toPointF(PointF& point) const;
    [[nodiscard]] SDL_Point toSdlPoint() const;
    SDL_Point& toSdlPoint(SDL_Point& point) const;
    [[nodiscard]] SDL_FPoint toSdlFPoint() const;
    SDL_FPoint& toSdlFPoint(SDL_FPoint& point) const;

private:
    NomadInteger m_x;
    NomadInteger m_y;
};

inline bool operator==(const Point& lhs, const Point& rhs) {
    return lhs.getX() == rhs.getX() && lhs.getY() == rhs.getY();
}

inline bool operator!=(const Point& lhs, const Point& rhs) {
    return !(lhs == rhs);
}

} // nomad
