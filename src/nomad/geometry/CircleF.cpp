// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/geometry/CircleF.hpp>

namespace nomad {

CircleF::CircleF(NomadFloat x, NomadFloat y, NomadFloat radius):
    m_center(x, y),
    m_radius(radius)
{
}

NomadFloat CircleF::getX() const {
    return m_center.getX();
}

NomadFloat CircleF::getY() const {
    return m_center.getY();
}

NomadFloat CircleF::getRadius() const {
    return m_radius;
}

const PointF& CircleF::getCenter() const {
    return m_center;
}

NomadFloat CircleF::getLeft() const {
    return m_center.getX() - m_radius;
}

NomadFloat CircleF::getRight() const {
    return m_center.getX() + m_radius;
}

NomadFloat CircleF::getTop() const {
    return m_center.getY() - m_radius;
}

NomadFloat CircleF::getBottom() const {
    return m_center.getY() + m_radius;
}

void CircleF::setX(NomadFloat x) {
    m_center.setX(x);
}

void CircleF::setY(NomadFloat y) {
    m_center.setY(y);
}

void CircleF::setRadius(NomadFloat radius) {
    m_radius = radius;
}

} // namespace nomad
