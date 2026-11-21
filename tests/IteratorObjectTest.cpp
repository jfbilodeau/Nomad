// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <FailingIteratorObject.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/RuntimeValueGuard.hpp>

#include <boost/test/unit_test.hpp>

#include <array>

using namespace nomad;

BOOST_AUTO_TEST_SUITE(iterator_objects)

BOOST_AUTO_TEST_CASE(partial_advancement_failure_discards_current_and_allows_retry) {
    Runtime runtime;
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (const auto* type : std::array<const Type*, 2>{runtime.getIntegerType(), runtime.getStringType()}) {
        RuntimeValue source;
        const RuntimeValueGuard sourceGuard(*type, &source);
        if (type == runtime.getStringType()) {
            source.setStringValue("retained iterator value");
        } else {
            source.setIntegerValue(17);
        }

        nomad::test::FailingIteratorObject iterator(type, source);
        BOOST_CHECK_THROW(static_cast<void>(iterator.getCurrent()), NomadException);
        BOOST_REQUIRE(iterator.moveNext());
        if (type == runtime.getStringType()) {
            BOOST_TEST(iterator.getCurrent().getObjectValue() == source.getObjectValue());
            BOOST_TEST(source.getObjectValue()->getReferenceCount() == 2U);
        } else {
            BOOST_TEST(iterator.getCurrent().getIntegerValue() == 17);
        }

        BOOST_CHECK_THROW(static_cast<void>(iterator.moveNext()), NomadException);
        BOOST_CHECK_THROW(static_cast<void>(iterator.getCurrent()), NomadException);
        BOOST_TEST(iterator.getCallCount() == 2U);
        if (type == runtime.getStringType()) {
            BOOST_TEST(source.getObjectValue()->getReferenceCount() == 1U);
        }

        BOOST_REQUIRE(iterator.moveNext());
        BOOST_TEST(iterator.getCallCount() == 3U);
        if (type == runtime.getStringType()) {
            BOOST_TEST(iterator.getCurrent().getStringValue() == "retained iterator value");
            BOOST_TEST(source.getObjectValue()->getReferenceCount() == 2U);
        } else {
            BOOST_TEST(iterator.getCurrent().getIntegerValue() == 17);
        }

        BOOST_TEST(!iterator.moveNext());
        BOOST_CHECK_THROW(static_cast<void>(iterator.getCurrent()), NomadException);
        BOOST_TEST(!iterator.moveNext());
        BOOST_TEST(iterator.getCallCount() == 4U);
        if (type == runtime.getStringType()) {
            BOOST_TEST(source.getObjectValue()->getReferenceCount() == 1U);
        }
    }

    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_SUITE_END()
