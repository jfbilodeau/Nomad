// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <boost/test/unit_test.hpp>

#include <type_traits>

using namespace nomad;

static_assert(!std::is_convertible_v<NomadId, PropertyId>);
static_assert(!std::is_convertible_v<PropertyId, NomadId>);
static_assert(!std::is_convertible_v<PropertyId, CallableId>);
static_assert(!std::is_copy_constructible_v<Property>);
static_assert(!std::is_constructible_v<ObjectType, NomadString>);
static_assert(!std::is_constructible_v<ObjectType, NomadString, bool>);
static_assert(std::is_constructible_v<ObjectType, NomadString, ObjectTypeMode>);
static_assert(static_cast<int>(ObjectTypeMode::Opened) == 1);
static_assert(static_cast<int>(ObjectTypeMode::Closed) == 2);

namespace {

const ObjectType* createTestType(Runtime& runtime, const NomadString& name, const ObjectTypeMode mode) {
    const auto id = runtime.createObjectType(name, mode);
    BOOST_REQUIRE(id.isValid());
    const auto type = runtime.getType(id);
    BOOST_REQUIRE(type.has_value());
    return static_cast<const ObjectType*>(*type);
}

} // namespace

BOOST_AUTO_TEST_SUITE(object_type)

BOOST_AUTO_TEST_CASE(open_type_registers_and_reuses_property_ids) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Point", ObjectTypeMode::Opened);
    BOOST_TEST(type->isOpen());
    const auto xId = runtime.registerProperty(type, "x", runtime.getFloatType());
    const auto yId = runtime.registerProperty(type, "y", runtime.getFloatType());
    BOOST_REQUIRE(xId.isValid());
    BOOST_REQUIRE(yId.isValid());
    BOOST_TEST(xId != yId);
    BOOST_TEST(runtime.registerProperty(type, "x", runtime.getFloatType()) == xId);
    BOOST_TEST(type->getPropertyCount() == 2u);

    const auto* xProperty = type->getProperty("x");
    BOOST_REQUIRE(xProperty != nullptr);
    BOOST_TEST(xProperty->getValueType() == runtime.getFloatType());
    BOOST_TEST(xProperty->getId() == xId);
    BOOST_TEST(type->getProperty(yId)->getName() == "y");
    BOOST_TEST(type->getProperty(PropertyId::INVALID) == nullptr);
    BOOST_TEST(runtime.getProperty(xId) == xProperty);
}

BOOST_AUTO_TEST_CASE(closed_type_can_register_declared_properties) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "ClosedPoint", ObjectTypeMode::Closed);
    BOOST_TEST(!type->isOpen());
    BOOST_TEST(runtime.registerProperty(type, "x", runtime.getFloatType()).isValid());
    BOOST_TEST(type->getPropertyCount() == 1u);
}

BOOST_AUTO_TEST_CASE(property_type_cannot_be_redefined) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Point", ObjectTypeMode::Opened);
    BOOST_REQUIRE(runtime.registerProperty(type, "x", runtime.getFloatType()).isValid());
    BOOST_TEST(runtime.registerProperty(type, "x", runtime.getIntegerType()).isInvalid());
    BOOST_TEST(type->getPropertyCount() == 1u);
    BOOST_TEST(type->getProperty("x")->getValueType() == runtime.getFloatType());
}

BOOST_AUTO_TEST_CASE(null_property_type_is_rejected) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Point", ObjectTypeMode::Opened);
    BOOST_TEST(runtime.registerProperty(type, "x", nullptr).isInvalid());
    BOOST_TEST(type->getPropertyCount() == 0u);
    BOOST_TEST(type->getProperty("x") == nullptr);
}

BOOST_AUTO_TEST_CASE(native_and_script_properties_share_a_stable_type_owned_catalog) {
    Runtime runtime;
    const auto* first = createTestType(runtime, "First", ObjectTypeMode::Opened);
    const auto* second = createTestType(runtime, "Second", ObjectTypeMode::Opened);
    const auto scriptId = runtime.registerProperty(first, "value", runtime.getFloatType());
    const auto nativeId = runtime.registerNativeProperty(
        second, "value", runtime.getIntegerType(),
        [](VirtualMachine*, RuntimeValue& result) { result.setIntegerValue(42); }, {}, "Native value"
    );
    BOOST_REQUIRE(scriptId.isValid());
    BOOST_REQUIRE(nativeId.isValid());
    BOOST_TEST(scriptId != nativeId);
    const auto* scriptProperty = runtime.getProperty(scriptId);
    const auto* nativeProperty = runtime.getNativeProperty(nativeId);
    BOOST_REQUIRE(scriptProperty != nullptr);
    BOOST_REQUIRE(nativeProperty != nullptr);
    BOOST_TEST(first->getProperty(scriptId) == scriptProperty);
    BOOST_TEST(second->getProperty(nativeId) == nativeProperty);
    BOOST_TEST(first->getProperty(nativeId) == nullptr);
    BOOST_TEST(runtime.getProperty(nativeId) == nativeProperty);
    BOOST_TEST(runtime.getNativeProperty(scriptId) == nullptr);
    BOOST_TEST((runtime.getMemberType(scriptId) == MemberType::Property));
    BOOST_TEST(nativeProperty->getDoc() == "Native value");
    BOOST_TEST(first->getProperties() == std::vector<const Property*>({scriptProperty}));
    BOOST_TEST(std::holds_alternative<PropertyId>(runtime.getNativeMemberId(second, "value")));
    const auto info = runtime.getNativePropertyInfo(nativeId);
    BOOST_REQUIRE(info.has_value());
    BOOST_TEST(info->id == nativeId);
    BOOST_TEST(!runtime.getNativePropertyInfo(scriptId).has_value());

    for (auto index = 0; index < 64; ++index) {
        BOOST_REQUIRE(runtime.registerProperty(first, "extra" + std::to_string(index), runtime.getFloatType()).isValid());
    }
    BOOST_TEST(runtime.getProperty(scriptId) == scriptProperty);
    BOOST_TEST(runtime.getNativeProperty(nativeId) == nativeProperty);
    BOOST_TEST(first->getProperty(scriptId) == scriptProperty);
    for (const auto id : {PropertyId::INVALID, PropertyId{-2}, PropertyId{NOMAD_ID_MAX - 1}, PropertyId{NOMAD_ID_MAX}}) {
        BOOST_TEST(runtime.getProperty(id) == nullptr);
        BOOST_TEST(runtime.getNativeProperty(id) == nullptr);
        BOOST_TEST(first->getProperty(id) == nullptr);
    }
}

BOOST_AUTO_TEST_CASE(properties_require_registered_owners_and_types_and_reject_member_collisions) {
    Runtime runtime;
    Runtime other;
    ObjectType unregistered("Unregistered", ObjectTypeMode::Opened);
    const auto* type = createTestType(runtime, "Point", ObjectTypeMode::Opened);
    const auto* foreign = createTestType(other, "Point", ObjectTypeMode::Opened);
    BOOST_TEST(runtime.registerProperty(nullptr, "value", runtime.getIntegerType()).isInvalid());
    BOOST_TEST(runtime.registerProperty(&unregistered, "value", runtime.getIntegerType()).isInvalid());
    BOOST_TEST(runtime.registerProperty(foreign, "value", runtime.getIntegerType()).isInvalid());
    BOOST_TEST(runtime.registerProperty(type, "value", other.getIntegerType()).isInvalid());
    BOOST_TEST(runtime.registerProperty(type, "value", runtime.getVoidType()).isInvalid());
    BOOST_TEST(runtime.registerProperty(type, "invalid.name", runtime.getIntegerType()).isInvalid());
    const auto method = runtime.registerNativeMethod(
        type, "move", [](VirtualMachine*) {}, {}, runtime.getVoidType(), ""
    );
    BOOST_REQUIRE(method.isValid());
    BOOST_TEST(runtime.registerProperty(type, "move", runtime.getFloatType()).isInvalid());
    BOOST_REQUIRE(runtime.registerProperty(type, "x", runtime.getFloatType()).isValid());
    BOOST_TEST(runtime.registerNativeMethod(type, "x", [](VirtualMachine*) {}, {}, runtime.getVoidType(), "").isInvalid());
    BOOST_TEST(runtime.registerNativeProperty(type, "x", runtime.getIntegerType(),
        [](VirtualMachine*, RuntimeValue&) {}, {}, "").isInvalid());
    const auto nativeId = runtime.registerNativeProperty(type, "native", runtime.getIntegerType(),
        [](VirtualMachine*, RuntimeValue&) {}, {}, "");
    BOOST_REQUIRE(nativeId.isValid());
    BOOST_TEST(runtime.registerProperty(type, "native", runtime.getIntegerType()).isInvalid());
    BOOST_TEST(type->getPropertyCount() == 2u);
    Property standalone(type, "standalone", runtime.getIntegerType());
    BOOST_TEST(standalone.registerWithRuntime(runtime).isInvalid());
    BOOST_TEST(standalone.getId().isInvalid());
}

BOOST_AUTO_TEST_CASE(type_owned_native_property_callbacks_are_destroyed_once) {
    auto destructions = 0;
    std::weak_ptr<int> observer;
    {
        Runtime runtime;
        const auto* type = createTestType(runtime, "Owner", ObjectTypeMode::Opened);
        auto probe = std::shared_ptr<int>(new int{42}, [&destructions](int* value) {
            ++destructions;
            delete value;
        });
        observer = probe;
        const auto id = runtime.registerNativeProperty(type, "value", runtime.getIntegerType(),
            [probe](VirtualMachine*, RuntimeValue& value) { value.setIntegerValue(*probe); }, {}, "");
        BOOST_REQUIRE(id.isValid());
        probe.reset();
        BOOST_TEST(!observer.expired());
        BOOST_TEST(destructions == 0);
    }
    BOOST_TEST(observer.expired());
    BOOST_TEST(destructions == 1);
}

BOOST_AUTO_TEST_CASE(native_object_types_use_explicit_modes_without_special_subclasses) {
    ObjectType nativeType("NativeType", ObjectTypeMode::Closed);
    ObjectType openNativeType("OpenNativeType", ObjectTypeMode::Opened);

    BOOST_TEST(!nativeType.isOpen());
    BOOST_TEST(openNativeType.isOpen());
}

BOOST_AUTO_TEST_SUITE_END()
