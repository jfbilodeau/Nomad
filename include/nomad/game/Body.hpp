// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/game/BodyType.hpp>
#include <nomad/geometry/PointF.hpp>

namespace nomad {

// Forward declarations
class CircleF;
class RectangleF;

enum class BodyShape {
    None = 1,
    Rectangle,
    Circle,
};

class Body {
public:
    Body() = default;
    Body(const Body& other) = default;
    Body(NomadFloat x, NomadFloat y);
    Body(NomadFloat x, NomadFloat y, NomadFloat width, NomadFloat height);
    Body(NomadFloat x, NomadFloat y, NomadFloat radius);

    ~Body() = default;

    [[nodiscard]] BodyShape getShape() const;

    [[nodiscard]] BodyType getType() const;

    [[nodiscard]] NomadFloat getX() const;
    void setX(NomadFloat x);

    [[nodiscard]]
    NomadFloat getY() const;
    void setY(NomadFloat y);

    [[nodiscard]]
    const PointF& getLocation() const;
    void set_location(NomadFloat x, NomadFloat y);
    void set_location(const PointF& location);

    void translate(NomadFloat deltaX, NomadFloat deltaY);
    void translate(const PointF& delta);

    [[nodiscard]] NomadFloat getWidth() const;

    [[nodiscard]] NomadFloat getHeight() const;

    [[nodiscard]] NomadFloat getRadius() const;

    void setNoBody();
    void setRectangleBody(BodyType type, NomadFloat width, NomadFloat height);
    void setCircleBody(BodyType type, NomadFloat radius);

    [[nodiscard]] bool contains(const PointF& point) const;
    [[nodiscard]] bool intersects(const Body& other) const;
    [[nodiscard]] bool intersects(const RectangleF& rectangle) const;
    [[nodiscard]] bool intersects(const CircleF& circle) const;

    Body& operator=(const Body& other) = default;

private:
    BodyShape m_shape = BodyShape::None;
    BodyType m_type = BodyType::Static;
    PointF m_location = {};
    NomadFloat m_width = 0.0f;
    NomadFloat m_height = 0.0f;
    NomadFloat m_radius = 0.0f;

    NomadBoolean m_bodyValid = false;
};

} // namespace nomad
