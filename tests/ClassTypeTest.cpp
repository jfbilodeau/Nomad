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
static_assert(!std::is_default_constructible_v<IntegerType>);
static_assert(!std::is_constructible_v<ClassType, NomadString, ClassMode>);
static_assert(!std::is_constructible_v<ClassType, Runtime*, NomadString>);
static_assert(!std::is_constructible_v<ClassType, Runtime*, NomadString, bool>);
static_assert(!std::is_constructible_v<ClassType, Runtime*, NomadString, ClassMode>);
static_assert(std::is_constructible_v<ClassType, Runtime*, TypeId, NomadString, ClassMode>);
static_assert(static_cast<int>(ClassMode::Opened) == 1);
static_assert(static_cast<int>(ClassMode::Closed) == 2);

namespace {

const ClassType* createTestType(Runtime& runtime, const NomadString& name, const ClassMode mode) {
    const auto id = runtime.registerClass(name, mode);
    BOOST_REQUIRE(id.isValid());
    const auto type = runtime.getType(id);
    BOOST_REQUIRE(type.has_value());
    return static_cast<const ClassType*>(*type);
}

} // namespace

BOOST_AUTO_TEST_SUITE(class_type)

BOOST_AUTO_TEST_CASE(class_conversion_returns_the_original_descriptor_for_classes_and_derived_types) {
    Runtime runtime;
    const Type* opened = createTestType(runtime, "Opened", ClassMode::Opened);
    const Type* closed = createTestType(runtime, "Closed", ClassMode::Closed);
    BOOST_TEST(opened->asClassType() == opened);
    BOOST_TEST(closed->asClassType() == closed);
    const Type* stringType = runtime.getStringType();
    BOOST_TEST(stringType->asClassType() == runtime.getStringType());
    const auto* fileNameType = runtime.getFileNameType();
    BOOST_TEST(fileNameType->asClassType() == fileNameType);
}

BOOST_AUTO_TEST_CASE(class_conversion_returns_null_for_non_class_descriptors) {
    Runtime runtime;
    const auto enumId = runtime.createEnum("State", {{"Ready", 1}});
    BOOST_REQUIRE(enumId.isValid());
    for (const auto* type : {
        runtime.getVoidType(), runtime.getIntegerType(), runtime.getFloatType(), runtime.getBooleanType(),
        runtime.getType(enumId).value(),
        runtime.getCallbackType({runtime.getIntegerType()}, runtime.getBooleanType())
    }) {
        BOOST_REQUIRE(type != nullptr);
        BOOST_TEST_CONTEXT(type->getTypeName()) {
            BOOST_TEST(type->asClassType() == nullptr);
        }
    }
}

BOOST_AUTO_TEST_CASE(open_type_registers_and_reuses_property_ids) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Point", ClassMode::Opened);
    BOOST_TEST(type->isOpen());
    const auto xId = runtime.registerProperty(type->getId(), "x", runtime.getFloatType());
    const auto yId = runtime.registerProperty(type->getId(), "y", runtime.getFloatType());
    BOOST_REQUIRE(xId.isValid());
    BOOST_REQUIRE(yId.isValid());
    BOOST_TEST(xId != yId);
    BOOST_TEST(runtime.registerProperty(type->getId(), "x", runtime.getFloatType()) == xId);
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
    const auto* type = createTestType(runtime, "ClosedPoint", ClassMode::Closed);
    BOOST_TEST(!type->isOpen());
    BOOST_TEST(runtime.registerProperty(type->getId(), "x", runtime.getFloatType()).isValid());
    BOOST_TEST(type->getPropertyCount() == 1u);
}

BOOST_AUTO_TEST_CASE(explicit_initializer_metadata_is_owned_by_each_stored_property) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Fields", ClassMode::Opened);
    const auto firstId = runtime.registerProperty(type->getId(), "first", runtime.getIntegerType());
    const auto secondId = runtime.registerProperty(type->getId(), "second", runtime.getStringType());
    BOOST_REQUIRE(firstId.isValid());
    BOOST_REQUIRE(secondId.isValid());
    const auto* first = runtime.getProperty(firstId);
    const auto* second = runtime.getProperty(secondId);
    BOOST_TEST(!first->hasExplicitInitializer());
    BOOST_TEST(!second->hasExplicitInitializer());
    runtime.setPropertyHasExplicitInitializer(firstId, true);
    BOOST_TEST(first->hasExplicitInitializer());
    BOOST_TEST(!second->hasExplicitInitializer());
    BOOST_TEST(runtime.registerProperty(type->getId(), "first", runtime.getIntegerType()) == firstId);
    BOOST_TEST(first->hasExplicitInitializer());
    runtime.setPropertyHasExplicitInitializer(firstId, false);
    BOOST_TEST(!first->hasExplicitInitializer());

    const auto nativeId = runtime.addProperty(type->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .name = "native",
        .getter = [](VirtualMachine*, RuntimeValue& result) { result.setIntegerValue(42); }
    });
    BOOST_REQUIRE(nativeId.isValid());
    BOOST_CHECK_THROW(runtime.setPropertyHasExplicitInitializer(nativeId, true), NomadBug);
    BOOST_TEST(!runtime.getProperty(nativeId)->hasExplicitInitializer());
    for (const auto id : {PropertyId::INVALID, PropertyId{-2}, PropertyId{NOMAD_ID_MAX}}) {
        BOOST_CHECK_THROW(runtime.setPropertyHasExplicitInitializer(id, true), NomadBug);
    }
}

BOOST_AUTO_TEST_CASE(property_type_cannot_be_redefined) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Point", ClassMode::Opened);
    BOOST_REQUIRE(runtime.registerProperty(type->getId(), "x", runtime.getFloatType()).isValid());
    BOOST_TEST(runtime.registerProperty(type->getId(), "x", runtime.getIntegerType()).isInvalid());
    BOOST_TEST(type->getPropertyCount() == 1u);
    BOOST_TEST(type->getProperty("x")->getValueType() == runtime.getFloatType());
}

BOOST_AUTO_TEST_CASE(null_property_type_is_rejected) {
    Runtime runtime;
    const auto* type = createTestType(runtime, "Point", ClassMode::Opened);
    BOOST_TEST(runtime.registerProperty(type->getId(), "x", nullptr).isInvalid());
    BOOST_TEST(type->getPropertyCount() == 0u);
    BOOST_TEST(type->getProperty("x") == nullptr);
}

BOOST_AUTO_TEST_CASE(native_and_script_properties_share_a_stable_type_owned_catalog) {
    Runtime runtime;
    const auto* first = createTestType(runtime, "First", ClassMode::Opened);
    const auto* second = createTestType(runtime, "Second", ClassMode::Opened);
    const auto scriptId = runtime.registerProperty(first->getId(), "value", runtime.getFloatType());
    const auto nativeId = runtime.addProperty(second->getId(), {
        .valueTypeId = runtime.getTypeId("int"),
        .name = "value",
        .getter = [](VirtualMachine*, RuntimeValue& result) { result.setIntegerValue(42); },
        .setter = {},
        .documentation = "Native value"
    });
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
        BOOST_REQUIRE(runtime.registerProperty(first->getId(), "extra" + std::to_string(index), runtime.getFloatType()).isValid());
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
    const auto* type = createTestType(runtime, "Point", ClassMode::Opened);
    for (const auto id : {TypeId::INVALID, TypeId{-2}, TypeId{NOMAD_ID_MAX}, runtime.getIntegerType()->getId()}) {
        BOOST_TEST(runtime.registerProperty(id, "value", runtime.getIntegerType()).isInvalid());
    }
    BOOST_TEST(runtime.registerProperty(type->getId(), "value", other.getIntegerType()).isInvalid());
    BOOST_TEST(runtime.registerProperty(type->getId(), "value", runtime.getVoidType()).isInvalid());
    BOOST_TEST(runtime.registerProperty(type->getId(), "invalid.name", runtime.getIntegerType()).isInvalid());
    const auto method = runtime.addMethod(type->getId(), {
        .name = "move",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("void"),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    });
    BOOST_REQUIRE(method.isValid());
    BOOST_TEST(runtime.registerProperty(type->getId(), "move", runtime.getFloatType()).isInvalid());
    BOOST_REQUIRE(runtime.registerProperty(type->getId(), "x", runtime.getFloatType()).isValid());
    BOOST_TEST(runtime.addMethod(type->getId(), {
        .name = "x",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("void"),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    }).isInvalid());
    const auto typeId = type->getId();
    BOOST_TEST(runtime.addProperty(typeId, {
        .valueTypeId = runtime.getTypeId("int"),
        .name = "x",
        .getter = [](VirtualMachine*, RuntimeValue&) {},
        .setter = {},
        .documentation = ""
    }).isInvalid());
    const auto nativeId = runtime.addProperty(typeId, {
        .valueTypeId = runtime.getTypeId("int"),
        .name = "native",
        .getter = [](VirtualMachine*, RuntimeValue&) {},
        .setter = {},
        .documentation = ""
    });
    BOOST_REQUIRE(nativeId.isValid());
    BOOST_TEST(runtime.registerProperty(type->getId(), "native", runtime.getIntegerType()).isInvalid());
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
        const auto* type = createTestType(runtime, "Owner", ClassMode::Opened);
        auto probe = std::shared_ptr<int>(new int{42}, [&destructions](int* value) {
            ++destructions;
            delete value;
        });
        observer = probe;
        const auto id = runtime.addProperty(type->getId(), {
            .valueTypeId = runtime.getTypeId("int"),
            .name = "value",
            .getter = [probe](VirtualMachine*, RuntimeValue& value) { value.setIntegerValue(*probe); },
            .setter = {},
            .documentation = ""
        });
        BOOST_REQUIRE(id.isValid());
        probe.reset();
        BOOST_TEST(!observer.expired());
        BOOST_TEST(destructions == 0);
    }
    BOOST_TEST(observer.expired());
    BOOST_TEST(destructions == 1);
}

BOOST_AUTO_TEST_CASE(native_class_types_use_explicit_modes_without_special_subclasses) {
    Runtime runtime;
    ClassType nativeType(&runtime, TypeId::INVALID, "NativeType", ClassMode::Closed);
    ClassType openNativeType(&runtime, TypeId::INVALID, "OpenNativeType", ClassMode::Opened);

    BOOST_TEST(!nativeType.isOpen());
    BOOST_TEST(openNativeType.isOpen());
}

BOOST_AUTO_TEST_SUITE_END()
