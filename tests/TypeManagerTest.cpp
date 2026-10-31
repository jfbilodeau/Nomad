// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Runtime.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/TypeManager.hpp>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace nomad;

static_assert(!std::is_copy_constructible_v<TypeManager>);
static_assert(!std::is_move_constructible_v<TypeManager>);
static_assert(std::is_same_v<decltype(std::declval<Runtime&>().getTypeManager()), TypeManager&>);
static_assert(std::is_same_v<decltype(std::declval<const Runtime&>().getTypeManager()), const TypeManager&>);

namespace {

class LifetimeType final : public ClassType {
public:
    LifetimeType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        bool& destroyed,
        NomadIndex& released
    ):
        ClassType(runtime, id, std::move(name), mode),
        m_destroyed(destroyed),
        m_released(released)
    {}

    ~LifetimeType() override {
        m_destroyed = true;
    }

    void freeValue(RuntimeValue* value) const override {
        BOOST_TEST(!m_destroyed);
        ++m_released;
        ClassType::freeValue(value);
    }

private:
    bool& m_destroyed;
    NomadIndex& m_released;
};

} // namespace

BOOST_AUTO_TEST_SUITE(type_manager)

BOOST_AUTO_TEST_CASE(runtime_composes_the_catalog_and_preserves_lookup_identity) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const Runtime& constRuntime = runtime;
    BOOST_TEST(&manager == &constRuntime.getTypeManager());
    BOOST_TEST(&manager.getRuntime() == &runtime);
    const auto* integer = runtime.getIntegerType();
    BOOST_TEST(manager.getTypeId("int") == integer->getId());
    BOOST_TEST(manager.getTypeByName("int") == integer);
    BOOST_REQUIRE(manager.getType(integer->getId()).has_value());
    BOOST_TEST(*manager.getType(integer->getId()) == *runtime.getType(integer->getId()));
    BOOST_TEST(manager.ownsType(integer));
    BOOST_TEST(!manager.ownsType(nullptr));
    Runtime other;
    BOOST_TEST(!manager.ownsType(other.getIntegerType()));
    BOOST_TEST(!manager.getType(TypeId::INVALID).has_value());
    BOOST_TEST(manager.getTypeByName("Missing") == nullptr);
    BOOST_TEST(manager.getTypeId("Missing").isInvalid());
    const auto classId = runtime.registerClass("CatalogClass", ClassMode::Closed);
    const auto structId = runtime.registerStruct("CatalogStruct");
    BOOST_REQUIRE(classId.isValid() && structId.isValid());
    const auto* originalClass = manager.getTypeByName("CatalogClass");
    const auto* originalStruct = manager.getTypeByName("CatalogStruct");
    for (auto index = NomadIndex{0}; index < 64; ++index) {
        BOOST_REQUIRE(runtime.registerStruct("Additional" + std::to_string(index)).isValid());
    }
    BOOST_TEST(manager.getTypeByName("CatalogClass") == originalClass);
    BOOST_TEST(manager.getTypeByName("CatalogStruct") == originalStruct);
    BOOST_TEST(*manager.getType(classId) == originalClass);
    BOOST_TEST(*manager.getType(structId) == originalStruct);
    std::vector<const Type*> runtimeTypes;
    std::vector<const Type*> managerTypes;
    runtime.getTypes(runtimeTypes);
    manager.getTypes(managerTypes);
    BOOST_TEST(runtimeTypes == managerTypes, boost::test_tools::per_element());
    manager.getTypes(managerTypes);
    BOOST_TEST(managerTypes.size() == runtimeTypes.size() * 2);
}

BOOST_AUTO_TEST_CASE(reserved_ids_are_absent_until_cataloged_and_invalid_publication_throws) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto previousCount = manager.getTypeCount();
    const auto id = manager.reserveTypeId();
    BOOST_TEST(manager.getTypeCount() == previousCount + 1);
    BOOST_TEST(!manager.getType(id).has_value());
    std::vector<const Type*> types;
    manager.getTypes(types);
    BOOST_TEST(types.size() == previousCount);
    BOOST_CHECK_THROW(manager.catalogType(nullptr), NomadException);
    BOOST_CHECK_THROW(manager.catalogType(std::make_unique<ClassType>(
        &runtime, TypeId::INVALID, "Invalid", ClassMode::Closed
    )), NomadException);
    Runtime other;
    BOOST_CHECK_THROW(manager.catalogType(std::make_unique<ClassType>(
        &other, id, "Foreign", ClassMode::Closed
    )), NomadException);
    auto descriptor = std::make_unique<ClassType>(&runtime, id, "CatalogOnly", ClassMode::Closed);
    const auto* original = descriptor.get();
    manager.catalogType(std::move(descriptor));
    BOOST_REQUIRE(manager.getType(id).has_value());
    BOOST_TEST(*manager.getType(id) == original);
    BOOST_TEST(runtime.getTypeByName("CatalogOnly") == original);
    BOOST_CHECK_THROW(manager.catalogType(std::make_unique<ClassType>(
        &runtime, id, "Replacement", ClassMode::Closed
    )), NomadException);
    BOOST_TEST(*manager.getType(id) == original);
}

BOOST_AUTO_TEST_CASE(manager_finalizes_nested_composites_and_validates_mutable_owner_access) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto innerId = runtime.registerStruct("Inner");
    const auto outerId = runtime.registerStruct("Outer");
    const auto* inner = manager.getTypeByName("Inner")->asStructType();
    const auto* outer = manager.getTypeByName("Outer")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(innerId, "value", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "inner", inner).isValid());
    manager.finalizeCompositeFieldLayout(outerId);
    BOOST_TEST(inner->isFieldLayoutFinalized());
    BOOST_TEST(outer->isFieldLayoutFinalized());
    BOOST_TEST(outer->getSize() == 1U);
    BOOST_TEST(manager.getMutableCompositeType(outerId) == outer);
    BOOST_TEST(manager.getMutableCompositeType(runtime.getIntegerType()->getId()) == nullptr);
    BOOST_TEST(manager.getMutableCompositeType(TypeId::INVALID) == nullptr);
    BOOST_CHECK_THROW(manager.finalizeCompositeFieldLayout(TypeId::INVALID), NomadException);
    BOOST_CHECK_THROW(manager.finalizeCompositeFieldLayout(runtime.getIntegerType()->getId()), NomadException);
    const auto classId = runtime.registerClass("Class", ClassMode::Closed);
    const auto* classType = manager.getTypeByName("Class")->asClassType();
    BOOST_TEST(manager.getMutableClassType(classType) == classType);
    Runtime other;
    BOOST_TEST(manager.getMutableClassType(other.getStringType()) == nullptr);
    BOOST_TEST(manager.getMutableClassType(nullptr) == nullptr);
    runtime.finalizeCompositeFieldLayout(classId);
    BOOST_TEST(classType->isFieldLayoutFinalized());
}

BOOST_AUTO_TEST_CASE(type_descriptors_outlive_runtime_owned_value_cleanup) {
    bool destroyed = false;
    NomadIndex released = 0;
    {
        Runtime runtime;
        const auto id = runtime.registerClass<LifetimeType>(
            "Lifetime", ClassMode::Closed, destroyed, released
        );
        BOOST_REQUIRE(id.isValid());
        const auto* type = runtime.getTypeManager().getTypeByName("Lifetime");
        RuntimeValue value;
        BOOST_REQUIRE(runtime.registerConstant("owned", value, type).isValid());
        auto context = std::make_unique<SimpleVariableContext>();
        const auto variableId = context->registerVariable("held", type);
        context->setValue(variableId, value);
        runtime.registerVariableContext("lifetime", "lifetime.", std::move(context));
        BOOST_TEST(!destroyed);
        released = 0;
    }
    BOOST_TEST(destroyed);
    BOOST_TEST(released >= 2U);
}

BOOST_AUTO_TEST_CASE(script_property_apis_enforce_owner_categories_and_value_contracts) {
    Runtime runtime;
    Runtime other;
    auto& manager = runtime.getTypeManager();
    const auto classId = runtime.registerClass("Fields", ClassMode::Closed);
    const auto structId = runtime.registerStruct("Record");
    BOOST_REQUIRE(classId.isValid() && structId.isValid());
    BOOST_TEST(manager.registerScriptClassProperty(structId, "wrong", runtime.getIntegerType()).isInvalid());
    BOOST_TEST(manager.registerScriptStructProperty(classId, "wrong", runtime.getIntegerType()).isInvalid());
    for (const auto ownerId : {classId, structId}) {
        const auto registerField = [&](const TypeId id, const NomadString& name, const Type* valueType) {
            return ownerId == classId ?
                manager.registerScriptClassProperty(id, name, valueType) :
                manager.registerScriptStructProperty(id, name, valueType);
        };
        BOOST_TEST(registerField(TypeId::INVALID, "value", runtime.getIntegerType()).isInvalid());
        BOOST_TEST(registerField(runtime.getIntegerType()->getId(), "value", runtime.getIntegerType()).isInvalid());
        const auto reservedId = manager.reserveTypeId();
        BOOST_TEST(registerField(reservedId, "value", runtime.getIntegerType()).isInvalid());
        BOOST_TEST(registerField(ownerId, "value", nullptr).isInvalid());
        BOOST_TEST(registerField(ownerId, "value", other.getIntegerType()).isInvalid());
        BOOST_TEST(registerField(ownerId, "value", runtime.getVoidType()).isInvalid());
        BOOST_REQUIRE(runtime.getFileNameType()->isInternal());
        BOOST_TEST(registerField(ownerId, "value", runtime.getFileNameType()).isInvalid());
        const auto* owner = manager.getType(ownerId).value()->asCompositeType();
        BOOST_TEST(owner->getPropertyCount() == 0U);
        const auto fieldId = registerField(ownerId, "value", runtime.getIntegerType());
        BOOST_REQUIRE(fieldId.isValid());
        const auto* property = runtime.getProperty(fieldId);
        BOOST_REQUIRE(property != nullptr);
        BOOST_TEST(property->getOwnerType() == owner);
        BOOST_TEST(property->getValueType() == runtime.getIntegerType());
        BOOST_TEST(property->asNativeProperty() == nullptr);
        BOOST_TEST(!property->hasExplicitInitializer());
        BOOST_TEST(registerField(ownerId, "value", runtime.getIntegerType()) == fieldId);
        BOOST_TEST(runtime.registerProperty(ownerId, "value", runtime.getIntegerType()) == fieldId);
        BOOST_TEST(registerField(ownerId, "value", runtime.getFloatType()).isInvalid());
        BOOST_TEST(owner->getPropertyCount() == 1U);
        BOOST_TEST(owner->getFieldIndex(fieldId) == 0U);
        manager.finalizeCompositeFieldLayout(ownerId);
        BOOST_TEST(registerField(ownerId, "later", runtime.getIntegerType()).isInvalid());
        BOOST_TEST(registerField(ownerId, "value", runtime.getIntegerType()) == fieldId);
        BOOST_TEST(owner->getPropertyCount() == 1U);
        BOOST_TEST(owner->getFieldOffset(fieldId) == 0U);
        BOOST_TEST(owner->getFieldStorageSize() == 1U);
        runtime.setPropertyHasExplicitInitializer(fieldId, true);
        BOOST_TEST(property->hasExplicitInitializer());
    }
}

BOOST_AUTO_TEST_CASE(script_field_registration_does_not_validate_identifier_syntax) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto classId = runtime.registerClass("Fields", ClassMode::Closed);
    const auto structId = runtime.registerStruct("Record");
    for (const auto* name : {"", "invalid.name", "1field", "bad name", "field@hidden", "not-a-field"}) {
        const auto classField = manager.registerScriptClassProperty(classId, name, runtime.getIntegerType());
        const auto structField = manager.registerScriptStructProperty(structId, name, runtime.getIntegerType());
        BOOST_REQUIRE(classField.isValid() && structField.isValid());
        BOOST_TEST(runtime.getProperty(classField)->getName() == name);
        BOOST_TEST(runtime.getProperty(structField)->getName() == name);
        BOOST_TEST(runtime.registerProperty(classId, name, runtime.getIntegerType()) == classField);
        BOOST_TEST(runtime.registerProperty(structId, name, runtime.getIntegerType()) == structField);
    }
}

BOOST_AUTO_TEST_CASE(script_fields_reject_native_and_script_member_collisions) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto classId = runtime.registerClass("Members", ClassMode::Opened);
    BOOST_REQUIRE(classId.isValid());
    const auto* owner = manager.getTypeByName("Members")->asClassType();
    const auto nativeProperty = runtime.addProperty(classId, {
        .valueType = runtime.getIntegerType(),
        .name = "nativeProperty",
        .getter = [](VirtualMachine*, RuntimeValue&) {}
    });
    const auto nativeMethod = runtime.addMethod(classId, {
        .name = "nativeMethod",
        .parameters = {},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [](VirtualMachine*) {},
        .documentation = ""
    });
    const auto scriptMethod = runtime.registerScriptMethod(classId, "scriptMethod", {}, "", "");
    BOOST_REQUIRE(nativeProperty.isValid() && nativeMethod.isValid() && scriptMethod.isValid());
    for (const auto* name : {"nativeProperty", "nativeMethod", "scriptMethod"}) {
        BOOST_TEST(manager.registerScriptClassProperty(classId, name, runtime.getIntegerType()).isInvalid());
    }
    BOOST_TEST(owner->getPropertyCount() == 1U);
    BOOST_TEST(owner->getFieldStorageSize() == 0U);
    BOOST_TEST(runtime.getNativeProperty(nativeProperty) != nullptr);
    const auto fieldId = manager.registerScriptClassProperty(classId, "stored", runtime.getIntegerType());
    BOOST_REQUIRE(fieldId.isValid());
    BOOST_TEST(runtime.addProperty(classId, {
        .valueType = runtime.getIntegerType(),
        .name = "stored",
        .getter = [](VirtualMachine*, RuntimeValue&) {}
    }).isInvalid());
    BOOST_TEST(owner->getPropertyCount() == 2U);
    BOOST_TEST(owner->getFieldIndex(fieldId) == 0U);
}

BOOST_AUTO_TEST_CASE(native_member_registration_does_not_validate_identifier_syntax) {
    for (const auto* name : {"", "invalid.name", "1member", "bad name", "member@hidden"}) {
        Runtime methodRuntime;
        const auto methodOwner = methodRuntime.registerClass("Methods", ClassMode::Closed);
        const auto methodId = methodRuntime.addMethod(methodOwner, {
            .name = name,
            .parameters = {},
            .returnTypeId = methodRuntime.getVoidType()->getId(),
            .function = [](VirtualMachine*) {},
            .documentation = ""
        });
        BOOST_REQUIRE(methodId.isValid());
        BOOST_TEST(methodRuntime.getNativeMethod(methodId)->getName() == name);
        Runtime propertyRuntime;
        const auto propertyOwner = propertyRuntime.registerClass("Properties", ClassMode::Closed);
        const auto propertyId = propertyRuntime.addProperty(propertyOwner, {
            .valueType = propertyRuntime.getIntegerType(),
            .name = name,
            .getter = [](VirtualMachine*, RuntimeValue&) {}
        });
        BOOST_REQUIRE(propertyId.isValid());
        BOOST_TEST(propertyRuntime.getProperty(propertyId)->getName() == name);
    }
}

BOOST_AUTO_TEST_CASE(explicit_script_fields_support_nested_and_empty_struct_storage) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto emptyId = runtime.registerStruct("Empty");
    const auto pointId = runtime.registerStruct("Point");
    const auto classId = runtime.registerClass("Player", ClassMode::Closed);
    const auto* empty = manager.getTypeByName("Empty");
    const auto* point = manager.getTypeByName("Point");
    const auto x = manager.registerScriptStructProperty(pointId, "x", runtime.getFloatType());
    const auto y = manager.registerScriptStructProperty(pointId, "y", runtime.getFloatType());
    const auto emptyField = manager.registerScriptStructProperty(pointId, "empty", empty);
    const auto name = manager.registerScriptClassProperty(classId, "name", runtime.getStringType());
    const auto location = manager.registerScriptClassProperty(classId, "location", point);
    BOOST_REQUIRE(x.isValid() && y.isValid() && emptyField.isValid() && name.isValid() && location.isValid());
    const auto* owner = manager.getTypeByName("Player")->asClassType();
    manager.finalizeCompositeFieldLayout(classId);
    BOOST_TEST(empty->getSize() == 0U);
    BOOST_TEST(point->getSize() == 2U);
    BOOST_TEST(point->asStructType()->getFieldIndex(emptyField) == 2U);
    BOOST_TEST(point->asStructType()->getFieldOffset(emptyField) == 2U);
    BOOST_TEST(owner->getFieldOffset(location) == 1U);
    BOOST_TEST(owner->getFieldStorageSize() == 3U);
    BOOST_TEST(owner->getSize() == 1U);
    BOOST_TEST(owner->getPropertyCount() == 2U);
}

BOOST_AUTO_TEST_SUITE_END()
