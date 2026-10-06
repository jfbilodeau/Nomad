// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/geometry/PointF.hpp>

namespace nomad {

class CircleF {
public:
    CircleF() = default;
    CircleF(NomadFloat x, NomadFloat y, NomadFloat radius);

    [[nodiscard]] NomadFloat getX() const;
    [[nodiscard]] NomadFloat getY() const;
    [[nodiscard]] NomadFloat getRadius() const;
    [[nodiscard]] const PointF& getCenter() const;
    [[nodiscard]] NomadFloat getLeft() const;
    [[nodiscard]] NomadFloat getRight() const;
    [[nodiscard]] NomadFloat getTop() const;
    [[nodiscard]] NomadFloat getBottom() const;

    void setX(NomadFloat x);
    void setY(NomadFloat y);
    void setRadius(NomadFloat radius);

private:
    PointF m_center;
    NomadFloat m_radius = 0;
};

} // namespace nomad
