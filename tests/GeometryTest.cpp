// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include "nomad/geometry/Point.hpp"
#include "nomad/geometry/PointF.hpp"
#include "nomad/geometry/Rectangle.hpp"
#include "nomad/geometry/RectangleF.hpp"
#include "nomad/geometry/Circle.hpp"
#include "nomad/geometry/CircleF.hpp"
#include "nomad/geometry/Intersection.hpp"

using namespace nomad;

BOOST_AUTO_TEST_CASE(point_operations)
{
    Point p1{1, 2};
    Point p2{3, 4};
    BOOST_TEST(p1.getX() != p2.getX());
    BOOST_TEST(p1.getY() != p2.getY());
}

BOOST_AUTO_TEST_CASE(pointf_operations)
{
    PointF p1{1.5f, 2.5f};
    PointF p2{1.5f, 2.5f};
    BOOST_TEST(p1.getX() == p2.getX());
    BOOST_TEST(p1.getY() == p2.getY());
}

BOOST_AUTO_TEST_CASE(rectangle_basic)
{
    Rectangle r{0, 0, 10, 10};
    Point p{5, 5};
    BOOST_TEST(r.contains(p));
}

BOOST_AUTO_TEST_CASE(rectanglef_basic)
{
    RectangleF r{0.0f, 0.0f, 5.0f, 5.0f};
    PointF p{2.5f, 2.5f};
    BOOST_TEST(r.contains(p));
}

BOOST_AUTO_TEST_CASE(circle_basic)
{
    Circle c{1, 2, 3};
    BOOST_TEST(c.getX() == 1);
    BOOST_TEST(c.getY() == 2);
    BOOST_TEST(c.getRadius() == 3);

    c.setX(4);
    c.setY(5);
    c.setRadius(6);

    BOOST_TEST(c.getX() == 4);
    BOOST_TEST(c.getY() == 5);
    BOOST_TEST(c.getRadius() == 6);
}

BOOST_AUTO_TEST_CASE(circlef_basic)
{
    CircleF c{1.5f, 2.5f, 3.5f};
    BOOST_TEST(c.getX() == 1.5f);
    BOOST_TEST(c.getY() == 2.5f);
    BOOST_TEST(c.getRadius() == 3.5f);

    c.setX(4.5f);
    c.setY(5.5f);
    c.setRadius(6.5f);

    BOOST_TEST(c.getX() == 4.5f);
    BOOST_TEST(c.getY() == 5.5f);
    BOOST_TEST(c.getRadius() == 6.5f);
}

BOOST_AUTO_TEST_CASE(circle_circle_intersection)
{
    CircleF a{0.0f, 0.0f, 5.0f};
    CircleF b{8.0f, 0.0f, 5.0f};
    // distance = 8, sum radii = 10 => intersect (8 < 10)
    BOOST_TEST(circleCircleIntersect(a, b));

    CircleF c{11.0f, 0.0f, 5.0f};
    // distance = 11, sum radii = 10 => do not intersect (11 >= 10)
    BOOST_TEST(!circleCircleIntersect(a, c));
}

BOOST_AUTO_TEST_CASE(circle_rectangle_intersection)
{
    CircleF circle{5.0f, 5.0f, 2.0f};
    RectangleF rect{0.0f, 0.0f, 10.0f, 10.0f};
    // circle center inside rectangle => intersection
    BOOST_TEST(circleRectangleIntersect(circle, rect));

    // circle just outside rectangle and not overlapping
    CircleF circle2{12.0f, 12.0f, 1.0f};
    BOOST_TEST(!circleRectangleIntersect(circle2, rect));
}

BOOST_AUTO_TEST_CASE(rectangle_rectangle_intersection_and_touch)
{
    RectangleF a{0.0f, 0.0f, 10.0f, 10.0f};
    RectangleF b{5.0f, 5.0f, 10.0f, 10.0f};
    // overlapping rectangles
    BOOST_TEST(rectangleRectangleIntersect(a, b));

    // touching at the edge (a.right == c.left) should NOT count as intersection
    RectangleF c{10.0f, 0.0f, 5.0f, 5.0f};
    BOOST_TEST(!rectangleRectangleIntersect(a, c));
}

BOOST_AUTO_TEST_CASE(point_in_circle_boundary)
{
    CircleF circle{0.0f, 0.0f, 5.0f};
    PointF inside{3.0f, 4.0f}; // distance = 5 => on boundary
    // pointOnBoundary should NOT be considered inside because pointInCircle uses strict <
    BOOST_TEST(!pointInCircle(circle, inside));

    PointF strictlyInside{0.0f, 0.0f};
    BOOST_TEST(pointInCircle(circle, strictlyInside));
}

BOOST_AUTO_TEST_CASE(point_in_rectangle_boundary)
{
    RectangleF rect{0.0f, 0.0f, 10.0f, 10.0f};
    PointF onEdge{10.0f, 5.0f};
    // Rectangle contains uses inclusive comparisons => boundary is contained
    BOOST_TEST(pointInRectangle(rect, onEdge));

    PointF outside{10.1f, 5.0f};
    BOOST_TEST(!pointInRectangle(rect, outside));
}
