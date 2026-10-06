// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <SDL3/SDL.h>

#include <nomad/geometry/PointF.hpp>
#include <nomad/geometry/Geometry.hpp>

namespace nomad {

class RectangleF {
public:
    RectangleF();
    RectangleF(Coord x, Coord y, Coord width, Coord height);
    RectangleF(const RectangleF& other) = default;
    RectangleF(RectangleF&& other) noexcept = default;

    RectangleF& operator=(const RectangleF& other) = default;
    RectangleF& operator=(RectangleF&& other) noexcept = default;

    [[nodiscard]] Coord getLeft() const {
        return m_x;
    }

    [[nodiscard]] Coord getRight() const {
        return m_x + m_width;
    }

    [[nodiscard]] Coord getTop() const {
        return m_y;
    }

    [[nodiscard]] Coord getBottom() const {
        return m_y + m_height;
    }

    [[nodiscard]] Coord getWidth() const {
        return m_width;
    }

    [[nodiscard]] Coord getHeight() const {
        return m_height;
    }

    [[nodiscard]] bool contains(const PointF& point) const {
        return point.getX() >= getLeft() && point.getX() <= getRight() && point.getY() >= getTop() && point.getY() <=
                                                                                                        getBottom();
    }

    [[nodiscard]] bool intersects(const RectangleF& other) const {
        return getLeft() < other.getRight() && getRight() > other.getLeft() && getTop() < other.getBottom() &&
                getBottom() >
                                                                                                                 other.getTop();
    }

    [[nodiscard]] RectangleF intersectionRect(const RectangleF& other) const {
        if (!intersects(other)) {
            return {};
        }

        return {
            std::max(getLeft(), other.getLeft()),
            std::max(getTop(), other.getTop()),
            std::min(getRight(), other.getRight()) - std::max(getLeft(), other.getLeft()),
            std::min(getBottom(), other.getBottom()) - std::max(getTop(), other.getTop())
        };
    }

    [[nodiscard]] RectangleF unionRect(const RectangleF& other) const {
        return {
            std::min(getLeft(), other.getLeft()),
            std::min(getTop(), other.getTop()),
            std::max(getRight(), other.getRight()) - std::min(getLeft(), other.getLeft()),
            std::max(getBottom(), other.getBottom()) - std::min(getTop(), other.getTop())
        };
    }

    RectangleF& move(Coord x, Coord y) {
        m_x += x;
        m_y += y;
        return *this;
    }

    RectangleF& move(const PointF& point) {
        return move(point.getX(), point.getY());
    }

    RectangleF& resize(Coord width, Coord height) {
        m_width += width;
        m_height += height;
        return *this;
    }

    RectangleF& resize(const PointF& point) {
        return resize(point.getX(), point.getY());
    }

    RectangleF& setLeft(Coord left) {
        m_x = left;
        return *this;
    }

    RectangleF& setRight(Coord right) {
        m_width = right - m_x;
        return *this;
    }

    RectangleF& setTop(Coord top) {
        m_y = top;
        return *this;
    }

    RectangleF& setBottom(Coord bottom) {
        m_height = bottom - m_y;
        return *this;
    }

    RectangleF& setWidth(Coord width) {
        m_width = width;
        return *this;
    }

    RectangleF& setHeight(Coord height) {
        m_height = height;
        return *this;
    }

    RectangleF& setPosition(Coord x, Coord y) {
        m_x = x;
        m_y = y;
        return *this;
    }

    RectangleF& setPosition(const PointF& point) {
        return setPosition(point.getX(), point.getY());
    }

    RectangleF& setSize(Coord width, Coord height) {
        m_width = width;
        m_height = height;
        return *this;
    }

    RectangleF& setSize(const PointF& point) {
        return setSize(point.getX(), point.getY());
    }

    RectangleF& set(Coord x, Coord y, Coord width, Coord height) {
        m_x = x;
        m_y = y;
        m_width = width;
        m_height = height;
        return *this;
    }

    RectangleF& set(const PointF& point, const PointF& size) {
        return set(point.getX(), point.getY(), size.getX(), size.getY());
    }

    RectangleF& set(const RectangleF& other) {
        return set(other.m_x, other.m_y, other.m_width, other.m_height);
    }

    RectangleF& set(const SDL_Rect& rect) {
        return set(
            static_cast<Coord>(rect.x),
            static_cast<Coord>(rect.y),
            static_cast<Coord>(rect.w),
            static_cast<Coord>(rect.h)
        );
    }

    RectangleF& set(const SDL_FRect& rect) {
        return set(rect.x, rect.y, rect.w, rect.h);
    }

    [[nodiscard]] SDL_Rect toSdlRect() const;
    [[nodiscard]] SDL_FRect toSdlFRect() const;

private:
    Coord m_x;
    Coord m_y;
    Coord m_width;
    Coord m_height;
};

} // nomad
