// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <SDL3/SDL.h>

#include <nomad/geometry/Point.hpp>
#include <nomad/geometry/Geometry.hpp>

namespace nomad {

class RectangleF;

class Rectangle {
public:
    Rectangle();
    Rectangle(NomadInteger x, NomadInteger y, NomadInteger width, NomadInteger height);
    Rectangle(const Rectangle& other) = default;
    Rectangle(Rectangle&& other) noexcept = default;

    Rectangle& operator=(const Rectangle& other) = default;
    Rectangle& operator=(Rectangle&& other) noexcept = default;

    [[nodiscard]] NomadInteger getLeft() const {
        return m_x;
    }

    [[nodiscard]] NomadInteger getRight() const {
        return m_x + m_width;
    }

    [[nodiscard]] NomadInteger getTop() const {
        return m_y;
    }

    [[nodiscard]] NomadInteger getBottom() const {
        return m_y + m_height;
    }

    [[nodiscard]] NomadInteger getWidth() const {
        return m_width;
    }

    [[nodiscard]] NomadInteger getHeight() const {
        return m_height;
    }

    [[nodiscard]] bool contains(const Point& point) const {
        return point.getX() >= getLeft() && point.getX() <= getRight() && point.getY() >= getTop() && point.getY() <=
                                                                                                        getBottom();
    }

    [[nodiscard]] bool Intersects(const Rectangle& other) const {
        return getLeft() < other.getRight() && getRight() > other.getLeft() && getTop() < other.getBottom() &&
                getBottom() >
                                                                                                                 other.getTop();
    }

    [[nodiscard]] Rectangle intersectionRect(const Rectangle& other) const {
        if (!Intersects(other)) {
            return {};
        }

        return {
            std::max(getLeft(), other.getLeft()),
            std::max(getTop(), other.getTop()),
            std::min(getRight(), other.getRight()) - std::max(getLeft(), other.getLeft()),
            std::min(getBottom(), other.getBottom()) - std::max(getTop(), other.getTop())
        };
    }

    [[nodiscard]] Rectangle unionRect(const Rectangle& other) const {
        return {
            std::min(getLeft(), other.getLeft()),
            std::min(getTop(), other.getTop()),
            std::max(getRight(), other.getRight()) - std::min(getLeft(), other.getLeft()),
            std::max(getBottom(), other.getBottom()) - std::min(getTop(), other.getTop())
        };
    }

    Rectangle& move(NomadInteger x, NomadInteger y) {
        m_x += x;
        m_y += y;
        return *this;
    }

    Rectangle& move(const Point& point) {
        return move(point.getX(), point.getY());
    }

    Rectangle& resize(NomadInteger width, NomadInteger height) {
        m_width += width;
        m_height += height;
        return *this;
    }

    Rectangle& resize(const Point& point) {
        return resize(point.getX(), point.getY());
    }

    Rectangle& setLeft(NomadInteger left) {
        m_x = left;
        return *this;
    }

    Rectangle& setRight(NomadInteger right) {
        m_width = right - m_x;
        return *this;
    }

    Rectangle& setTop(NomadInteger top) {
        m_y = top;
        return *this;
    }

    Rectangle& setBottom(NomadInteger bottom) {
        m_height = bottom - m_y;
        return *this;
    }

    Rectangle& setWidth(NomadInteger width) {
        m_width = width;
        return *this;
    }

    Rectangle& setHeight(NomadInteger height) {
        m_height = height;
        return *this;
    }

    Rectangle& setPosition(NomadInteger x, NomadInteger y) {
        m_x = x;
        m_y = y;
        return *this;
    }

    Rectangle& setPosition(const Point& point) {
        return setPosition(point.getX(), point.getY());
    }

    Rectangle& setSize(NomadInteger width, NomadInteger height) {
        m_width = width;
        m_height = height;
        return *this;
    }

    Rectangle& setSize(const Point& point) {
        return setSize(point.getX(), point.getY());
    }

    Rectangle& set(NomadInteger x, NomadInteger y, NomadInteger width, NomadInteger height) {
        m_x = x;
        m_y = y;
        m_width = width;
        m_height = height;
        return *this;
    }

    Rectangle& set(const Point& point, const Point& size) {
        return set(point.getX(), point.getY(), size.getX(), size.getY());
    }

    Rectangle& set(const Rectangle& other) {
        return set(other.m_x, other.m_y, other.m_width, other.m_height);
    }

    Rectangle& set(const SDL_Rect& rect) {
        return set(rect.x, rect.y, rect.w, rect.h);
    }

    Rectangle& set(const SDL_FRect& rect);

    [[nodiscard]] RectangleF toRectangleF() const;
    [[nodiscard]] SDL_Rect toSdlRect() const;
    [[nodiscard]] SDL_FRect toSdlFRect() const;

private:
    NomadInteger m_x;
    NomadInteger m_y;
    NomadInteger m_width;
    NomadInteger m_height;
};

} // nomad
