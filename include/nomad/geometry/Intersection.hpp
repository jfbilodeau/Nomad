// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/geometry/CircleF.hpp>
#include <nomad/geometry/RectangleF.hpp>

namespace nomad {

bool circleCircleIntersect(const CircleF& a, const CircleF& b);
bool circleRectangleIntersect(const CircleF& circle, const RectangleF& rectangle);
bool rectangleRectangleIntersect(const RectangleF& a, const RectangleF& b);

bool pointInCircle(const CircleF& circle, const PointF& point);
bool pointInRectangle(const RectangleF& rectangle, const PointF& point);

} // namespace nomad
