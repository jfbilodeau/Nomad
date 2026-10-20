// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/debug/DebugDraw.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;

BOOST_AUTO_TEST_SUITE(debug_draw)

BOOST_AUTO_TEST_CASE(initializes_every_box2d_debug_draw_option)
{
    auto debugDraw = b2DefaultDebugDraw();
    debugDraw.useDrawingBounds = true;
    debugDraw.drawBodyNames = true;
    debugDraw.drawContacts = true;
    debugDraw.drawContactImpulses = true;
    debugDraw.drawContactFeatures = true;
    debugDraw.drawFrictionImpulses = true;
    debugDraw.drawIslands = true;

    createDebugDraw(nullptr, &debugDraw);

    BOOST_TEST(!debugDraw.useDrawingBounds);
    BOOST_TEST(debugDraw.drawShapes);
    BOOST_TEST(debugDraw.drawJoints);
    BOOST_TEST(!debugDraw.drawJointExtras);
    BOOST_TEST(debugDraw.drawBounds);
    BOOST_TEST(!debugDraw.drawMass);
    BOOST_TEST(!debugDraw.drawBodyNames);
    BOOST_TEST(!debugDraw.drawContacts);
    BOOST_TEST(!debugDraw.drawGraphColors);
    BOOST_TEST(!debugDraw.drawContactNormals);
    BOOST_TEST(!debugDraw.drawContactImpulses);
    BOOST_TEST(!debugDraw.drawContactFeatures);
    BOOST_TEST(!debugDraw.drawFrictionImpulses);
    BOOST_TEST(!debugDraw.drawIslands);
    BOOST_TEST(debugDraw.context == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()
