// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptObjectType.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;

BOOST_AUTO_TEST_SUITE(script_object_type)

BOOST_AUTO_TEST_CASE(open_type_registers_and_reuses_property_ids) {
    ScriptObjectType type("Point");
    FloatType floatType;
    BOOST_TEST(type.isOpen());
    const auto xId = type.addProperty("x", &floatType);
    const auto yId = type.addProperty("y", &floatType);
    BOOST_REQUIRE(xId != NOMAD_INVALID_ID);
    BOOST_REQUIRE(yId != NOMAD_INVALID_ID);
    BOOST_TEST(xId == 0);
    BOOST_TEST(yId == 1);
    BOOST_TEST(type.addProperty("x", &floatType) == xId);
    BOOST_TEST(type.getPropertyCount() == 2u);

    const auto* xProperty = type.getProperty("x");
    BOOST_REQUIRE(xProperty != nullptr);
    BOOST_TEST(xProperty->type == &floatType);
    BOOST_TEST(xProperty->id == xId);
    BOOST_TEST(type.getProperty(yId)->name == "y");
    BOOST_TEST(type.getProperty(2) == nullptr);
}

BOOST_AUTO_TEST_CASE(closed_type_can_register_declared_properties) {
    ScriptObjectType type("ClosedPoint", false);
    FloatType floatType;
    BOOST_TEST(!type.isOpen());
    BOOST_TEST(type.addProperty("x", &floatType) != NOMAD_INVALID_ID);
    BOOST_TEST(type.getPropertyCount() == 1u);
}

BOOST_AUTO_TEST_CASE(property_type_cannot_be_redefined) {
    ScriptObjectType type("Point");
    FloatType floatType;
    IntegerType integerType;
    BOOST_REQUIRE(type.addProperty("x", &floatType) != NOMAD_INVALID_ID);
    BOOST_TEST(type.addProperty("x", &integerType) == NOMAD_INVALID_ID);
    BOOST_TEST(type.getPropertyCount() == 1u);
    BOOST_TEST(type.getProperty("x")->type == &floatType);
}

BOOST_AUTO_TEST_CASE(null_property_type_is_rejected) {
    ScriptObjectType type("Point");
    BOOST_TEST(type.addProperty("x", nullptr) == NOMAD_INVALID_ID);
    BOOST_TEST(type.getPropertyCount() == 0u);
    BOOST_TEST(type.getProperty("x") == nullptr);
}

BOOST_AUTO_TEST_CASE(native_object_types_remain_closed_by_default) {
    ObjectType nativeType("NativeType");
    class OpenNativeType final : public ObjectType {
    public:
        OpenNativeType(): ObjectType("OpenNativeType") {}
        bool isOpen() const override { return true; }
    };
    OpenNativeType openNativeType;

    BOOST_TEST(!nativeType.isOpen());
    BOOST_TEST(openNativeType.isOpen());
}

BOOST_AUTO_TEST_SUITE_END()
