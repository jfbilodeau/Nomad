// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/geometry/Circle.hpp>

namespace nomad {

Circle::Circle(NomadInteger x, NomadInteger y, NomadInteger radius):
    m_center(x, y),
    m_radius(radius)
{
}

NomadInteger Circle::getX() const {
    return m_center.getX();
}

NomadInteger Circle::getY() const {
    return m_center.getY();
}

NomadInteger Circle::getRadius() const {
    return m_radius;
}

const Point& Circle::getCenter() const {
    return m_center;
}

NomadInteger Circle::getLeft() const {
    return m_center.getX() - m_radius;
}

NomadInteger Circle::getRight() const {
    return m_center.getX() + m_radius;
}

NomadInteger Circle::getTop() const {
    return m_center.getY() - m_radius;
}

NomadInteger Circle::getBottom() const {
    return m_center.getY() + m_radius;
}

void Circle::setX(NomadInteger x) {
    m_center.setX(x);
}

void Circle::setY(NomadInteger y) {
    m_center.setY(y);
}

void Circle::setRadius(NomadInteger radius) {
    m_radius = radius;
}

} // namespace nomad
