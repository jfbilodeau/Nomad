// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/geometry/Intersection.hpp>

#include <algorithm>
#include <cmath>

namespace nomad {

bool circleCircleIntersect(const CircleF& a, const CircleF& b) {
    auto dx = a.getX() - b.getX();
    auto dy = a.getY() - b.getY();
    auto distance_squared = dx * dx + dy * dy;
    auto radius_sum = a.getRadius() + b.getRadius();
    auto radius_sum_squared = radius_sum * radius_sum;

    return distance_squared < radius_sum_squared;
}

bool circleRectangleIntersect(const CircleF& circle, const RectangleF& rectangle) {
    auto circle_x = circle.getX();
    auto circle_y = circle.getY();

    auto closest_x = std::clamp(circle_x, rectangle.getLeft(), rectangle.getRight());
    auto closest_y = std::clamp(circle_y, rectangle.getTop(), rectangle.getBottom());

    auto dx = circle_x - closest_x;
    auto dy = circle_y - closest_y;

    return dx * dx + dy * dy < circle.getRadius() * circle.getRadius();}

bool rectangleRectangleIntersect(const RectangleF& a, const RectangleF& b) {
    return a.getLeft() < b.getRight() &&
           a.getRight() > b.getLeft() &&
           a.getTop() < b.getBottom() &&
           a.getBottom() > b.getTop();
}

bool pointInCircle(const CircleF& circle, const PointF& point) {
    auto dx = circle.getX() - point.getX();
    auto dy = circle.getY() - point.getY();

    auto distance_squared = dx * dx + dy * dy;
    return distance_squared < circle.getRadius() * circle.getRadius();
}

bool pointInRectangle(const RectangleF& rectangle, const PointF& point) {
    return point.getX() >= rectangle.getLeft() &&
           point.getX() <= rectangle.getRight() &&
           point.getY() >= rectangle.getTop() &&
           point.getY() <= rectangle.getBottom();
}

} // namespace nomad
