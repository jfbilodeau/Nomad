// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include "nomad/game/EventRegistration.hpp"

using namespace nomad;

BOOST_AUTO_TEST_CASE(event_registration_replaces_and_removes_system_events)
{
    EventRegistrationManager manager;

    manager.registerSystemEvent(SystemEvent::Update, CallableId{10});
    BOOST_TEST(manager.getFunctionIdForSystemEvent(SystemEvent::Update) == CallableId{10});

    manager.registerSystemEvent(SystemEvent::Update, CallableId{11});
    BOOST_TEST(manager.getFunctionIdForSystemEvent(SystemEvent::Update) == CallableId{11});

    manager.unregisterSystemEvent(SystemEvent::Update);
    BOOST_TEST(manager.getFunctionIdForSystemEvent(SystemEvent::Update).isInvalid());
}

BOOST_AUTO_TEST_CASE(event_registration_replaces_and_removes_user_events)
{
    EventRegistrationManager manager;

    manager.registerUserEvent("interact", CallableId{20});
    BOOST_TEST(manager.getFunctionIdForUserEvent("interact") == CallableId{20});

    manager.registerUserEvent("interact", CallableId{21});
    BOOST_TEST(manager.getFunctionIdForUserEvent("interact") == CallableId{21});

    manager.unregisterUserEvent("interact");
    BOOST_TEST(manager.getFunctionIdForUserEvent("interact").isInvalid());
}

BOOST_AUTO_TEST_CASE(event_registration_removes_only_the_requested_mask_event)
{
    EventRegistrationManager manager;

    manager.registerMaskEvent(SystemEvent::BeginCollision, CallableId(30), 1);
    manager.registerMaskEvent(SystemEvent::BeginCollision, CallableId(31), 2);

    manager.unregisterMaskEvent(SystemEvent::BeginCollision, 1);

    BOOST_TEST(manager.getFunctionIdForMaskEvent(SystemEvent::BeginCollision, 1).isInvalid());
    BOOST_TEST(manager.getFunctionIdForMaskEvent(SystemEvent::BeginCollision, 2) == CallableId(31));
}
