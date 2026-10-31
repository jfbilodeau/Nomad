// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/BaseTypes.hpp>
#include <nomad/script/CompositeType.hpp>
#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

using namespace nomad;

static_assert(!std::is_convertible_v<NomadId, PropertyId>);
static_assert(!std::is_convertible_v<PropertyId, NomadId>);
static_assert(!std::is_convertible_v<PropertyId, CallableId>);
static_assert(!std::is_copy_constructible_v<Property>);
static_assert(std::is_abstract_v<CompositeType>);
static_assert(std::is_base_of_v<Type, CompositeType>);
static_assert(std::is_base_of_v<CompositeType, ClassType>);
static_assert(!std::is_invocable_v<decltype(&CompositeType::finalizeFieldLayout), const CompositeType&>);
static_assert(std::is_invocable_v<decltype(&Runtime::finalizeCompositeFieldLayout), Runtime&, TypeId>);
static_assert(!std::is_invocable_v<decltype(&Runtime::finalizeCompositeFieldLayout), const Runtime&, TypeId>);
static_assert(!std::is_invocable_v<decltype(&Runtime::finalizeCompositeFieldLayout), Runtime&, NomadId>);
static_assert(!std::is_default_constructible_v<IntegerType>);
static_assert(!std::is_constructible_v<ClassType, NomadString, ClassMode>);
static_assert(!std::is_constructible_v<ClassType, Runtime*, NomadString>);
static_assert(!std::is_constructible_v<ClassType, Runtime*, NomadString, bool>);
static_assert(!std::is_constructible_v<ClassType, Runtime*, NomadString, ClassMode>);
static_assert(std::is_constructible_v<ClassType, Runtime*, TypeId, NomadString, ClassMode>);
static_assert(static_cast<int>(ClassMode::Opened) == 1);
static_assert(static_cast<int>(ClassMode::Closed) == 2);

namespace {

class FieldLayoutTestType final : public ClassType {
public:
    FieldLayoutTestType(
        Runtime* runtime,
        const TypeId id,
        NomadString name,
        const ClassMode mode,
        FieldLayoutTestType*& descriptor,
        const std::optional<NomadIndex> size
    ):
        ClassType(runtime, id, std::move(name), mode),
        m_size(size)
    {
        descriptor = this;
    }

    [[nodiscard]] NomadIndex getSize() const override {
        ++m_sizeQueryCount;
        if (!m_size.has_value()) {
            throw NomadException("Test field size is unresolved");
        }
        return *m_size;
    }

    void setSize(const NomadIndex size) {
        m_size = size;
    }

    [[nodiscard]] NomadIndex getSizeQueryCount() const {
        return m_sizeQueryCount;
    }

private:
    std::optional<NomadIndex> m_size;
    mutable NomadIndex m_sizeQueryCount = 0;
};

class InlineLayoutTestType final : public ClassType {
public:
    InlineLayoutTestType(
        Runtime* runtime,
        const TypeId id,
        NomadString name,
        const ClassMode mode,
        InlineLayoutTestType*& descriptor
    ):
        ClassType(runtime, id, std::move(name), mode)
    {
        descriptor = this;
    }

    [[nodiscard]] TypeStorage getStorage() const override {
        return TypeStorage::Struct;
    }

    [[nodiscard]] NomadIndex getSize() const override {
        if (!isFieldLayoutFinalized()) {
            throw NomadException("Test inline field layout is unresolved");
        }
        return getFieldStorageSize();
    }
};

const ClassType* createTestType(Runtime& runtime, const NomadString& name, const ClassMode mode) {
    const auto id = runtime.registerClass(name, mode);
    BOOST_REQUIRE(id.isValid());
    const auto type = runtime.getType(id);
    BOOST_REQUIRE(type.has_value());
    return static_cast<const ClassType*>(*type);
}

} // namespace

BOOST_AUTO_TEST_SUITE(class_type)

BOOST_AUTO_TEST_CASE(runtime_finalization_rejects_invalid_and_non_composite_type_ids) {
    Runtime runtime;
    const auto* owner = createTestType(runtime, "Fields", ClassMode::Closed);
    BOOST_REQUIRE(runtime.registerProperty(owner->getId(), "value", runtime.getIntegerType()).isValid());
    const auto enumId = runtime.createEnum("State", {{"Ready", 1}});
    BOOST_REQUIRE(enumId.isValid());
    for (const auto id : {TypeId::INVALID, TypeId{-2}, TypeId{NOMAD_ID_MAX},
        runtime.getVoidType()->getId(), runtime.getIntegerType()->getId(), enumId}) {
        BOOST_CHECK_THROW(runtime.finalizeCompositeFieldLayout(id), NomadException);
        BOOST_TEST(!owner->isFieldLayoutFinalized());
    }
    runtime.finalizeCompositeFieldLayout(owner->getId());
    BOOST_TEST(owner->isFieldLayoutFinalized());
    BOOST_TEST(owner->getFieldStorageSize() == 1U);
}

BOOST_AUTO_TEST_CASE(explicit_finalization_freezes_stored_fields_but_not_values_or_native_members) {
    Runtime runtime;
    const auto* owner = createTestType(runtime, "Fields", ClassMode::Opened);
    const auto first = runtime.registerProperty(owner->getId(), "first", runtime.getFloatType());
    const auto second = runtime.registerProperty(owner->getId(), "second", runtime.getStringType());
    BOOST_REQUIRE(first.isValid());
    BOOST_REQUIRE(second.isValid());
    BOOST_TEST(!owner->isFieldLayoutFinalized());
    BOOST_TEST(owner->getFieldStorageSize() == 2U);
    BOOST_TEST(!owner->isFieldLayoutFinalized());

    runtime.finalizeCompositeFieldLayout(owner->getId());
    BOOST_TEST(owner->isFieldLayoutFinalized());
    BOOST_TEST(owner->getFieldStorageSize() == 2U);
    BOOST_TEST(owner->getFieldOffset(first) == 0U);
    BOOST_TEST(owner->getFieldOffset(second) == 1U);
    BOOST_TEST(owner->getSize() == 1U);
    runtime.finalizeCompositeFieldLayout(owner->getId());
    BOOST_TEST(runtime.registerProperty(owner->getId(), "first", runtime.getFloatType()) == first);
    BOOST_TEST(runtime.registerProperty(owner->getId(), "extra", runtime.getIntegerType()).isInvalid());
    BOOST_TEST(owner->getProperty("extra") == nullptr);
    BOOST_TEST(owner->getPropertyCount() == 2U);
    runtime.setPropertyHasExplicitInitializer(first, true);
    BOOST_TEST(owner->getProperty(first)->hasExplicitInitializer());
    ScriptObject object(owner);
    object.setField(owner->getFieldIndex(first), RuntimeValue(NomadFloat{3.5}));
    BOOST_TEST(object.getField(owner->getFieldIndex(first)).getFloatValue() == 3.5);
    object.setField(owner->getFieldIndex(first), RuntimeValue(NomadFloat{7.0}));
    BOOST_TEST(object.getField(owner->getFieldIndex(first)).getFloatValue() == 7.0);

    auto callbackCalls = 0;
    const auto native = runtime.addProperty(owner->getId(), {
        .valueType = runtime.getIntegerType(),
        .name = "native",
        .getter = [&](VirtualMachine*, RuntimeValue&) { ++callbackCalls; }
    });
    const auto method = runtime.addMethod(owner->getId(), {
        .name = "update",
        .parameters = {},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [&](VirtualMachine*) { ++callbackCalls; },
        .documentation = ""
    });
    const auto constructor = runtime.addConstructor(owner->getId(), {
        .parameters = {},
        .function = [&](VirtualMachine*) { ++callbackCalls; },
        .documentation = ""
    });
    BOOST_REQUIRE(native.isValid());
    BOOST_REQUIRE(method.isValid());
    BOOST_REQUIRE(constructor.isValid());
    BOOST_TEST(owner->isFieldLayoutFinalized());
    BOOST_TEST(owner->getPropertyCount() == 3U);
    BOOST_TEST(owner->getFieldStorageSize() == 2U);
    BOOST_TEST(owner->getFieldIndex(second) == 1U);
    BOOST_TEST(owner->getFieldOffset(second) == 1U);
    BOOST_CHECK_THROW((void)owner->getFieldOffset(native), NomadException);
    BOOST_TEST(callbackCalls == 0);
}

BOOST_AUTO_TEST_CASE(finalization_distinguishes_empty_inline_layouts_from_unresolved_layouts) {
    Runtime runtime;
    InlineLayoutTestType* empty = nullptr;
    BOOST_REQUIRE(runtime.registerClass<InlineLayoutTestType>(
        "EmptyInline", ClassMode::Closed, empty
    ).isValid());
    const auto* owner = createTestType(runtime, "Fields", ClassMode::Closed);
    const auto first = runtime.registerProperty(owner->getId(), "first", empty);
    const auto number = runtime.registerProperty(owner->getId(), "number", runtime.getIntegerType());
    const auto middle = runtime.registerProperty(owner->getId(), "middle", empty);
    const auto last = runtime.registerProperty(owner->getId(), "last", empty);
    BOOST_REQUIRE(first.isValid());
    BOOST_REQUIRE(number.isValid());
    BOOST_REQUIRE(middle.isValid());
    BOOST_REQUIRE(last.isValid());
    BOOST_TEST(!empty->isFieldLayoutFinalized());
    BOOST_CHECK_THROW((void)empty->getSize(), NomadException);
    BOOST_CHECK_THROW((void)owner->getFieldStorageSize(), NomadException);

    runtime.finalizeCompositeFieldLayout(owner->getId());
    BOOST_TEST(owner->isFieldLayoutFinalized());
    BOOST_TEST(empty->isFieldLayoutFinalized());
    BOOST_TEST(!empty->isVoid());
    BOOST_TEST(empty->getSize() == 0U);
    BOOST_TEST(owner->getFieldStorageSize() == 1U);
    BOOST_TEST(owner->getFieldOffset(first) == 0U);
    BOOST_TEST(owner->getFieldOffset(number) == 0U);
    BOOST_TEST(owner->getFieldOffset(middle) == 1U);
    BOOST_TEST(owner->getFieldOffset(last) == 1U);
    BOOST_TEST(owner->getFieldIndex(last) == 3U);
    runtime.finalizeCompositeFieldLayout(empty->getId());
    BOOST_TEST(empty->getSize() == 0U);
}

BOOST_AUTO_TEST_CASE(finalization_resolves_nested_inline_dependencies_before_containing_layouts) {
    Runtime runtime;
    InlineLayoutTestType* point = nullptr;
    InlineLayoutTestType* pair = nullptr;
    BOOST_REQUIRE(runtime.registerClass<InlineLayoutTestType>(
        "PointInline", ClassMode::Closed, point
    ).isValid());
    BOOST_REQUIRE(runtime.registerClass<InlineLayoutTestType>(
        "PairInline", ClassMode::Closed, pair
    ).isValid());
    for (const auto* name : {"x", "y", "z"}) {
        BOOST_REQUIRE(runtime.registerProperty(point->getId(), name, runtime.getFloatType()).isValid());
    }
    const auto origin = runtime.registerProperty(pair->getId(), "origin", point);
    const auto destination = runtime.registerProperty(pair->getId(), "destination", point);
    const auto label = runtime.registerProperty(pair->getId(), "label", runtime.getStringType());
    const auto* owner = createTestType(runtime, "Player", ClassMode::Closed);
    const auto name = runtime.registerProperty(owner->getId(), "name", runtime.getStringType());
    const auto location = runtime.registerProperty(owner->getId(), "location", pair);
    BOOST_REQUIRE(origin.isValid());
    BOOST_REQUIRE(destination.isValid());
    BOOST_REQUIRE(label.isValid());
    BOOST_REQUIRE(name.isValid());
    BOOST_REQUIRE(location.isValid());

    runtime.finalizeCompositeFieldLayout(owner->getId());
    BOOST_TEST(point->isFieldLayoutFinalized());
    BOOST_TEST(pair->isFieldLayoutFinalized());
    BOOST_TEST(point->getSize() == 3U);
    BOOST_TEST(pair->getSize() == 7U);
    BOOST_TEST(pair->getFieldOffset(origin) == 0U);
    BOOST_TEST(pair->getFieldOffset(destination) == 3U);
    BOOST_TEST(pair->getFieldOffset(label) == 6U);
    BOOST_TEST(owner->getFieldOffset(location) == 1U);
    BOOST_TEST(owner->getFieldStorageSize() == 8U);
    BOOST_TEST(owner->getSize() == 1U);
    BOOST_TEST(runtime.registerProperty(point->getId(), "extra", runtime.getIntegerType()).isInvalid());
    runtime.finalizeCompositeFieldLayout(owner->getId());
    BOOST_TEST(owner->getFieldStorageSize() == 8U);
}

BOOST_AUTO_TEST_CASE(finalization_rejects_direct_and_indirect_inline_cycles_without_committing_failed_layouts) {
    for (const auto indirect : {false, true}) {
        Runtime runtime;
        InlineLayoutTestType* first = nullptr;
        InlineLayoutTestType* second = nullptr;
        BOOST_REQUIRE(runtime.registerClass<InlineLayoutTestType>(
            "FirstInline", ClassMode::Closed, first
        ).isValid());
        BOOST_REQUIRE(runtime.registerClass<InlineLayoutTestType>(
            "SecondInline", ClassMode::Closed, second
        ).isValid());
        BOOST_REQUIRE(runtime.registerProperty(first->getId(), "number", runtime.getIntegerType()).isValid());
        BOOST_REQUIRE(runtime.registerProperty(first->getId(), "next", indirect ? second : first).isValid());
        if (indirect) {
            BOOST_REQUIRE(runtime.registerProperty(second->getId(), "next", first).isValid());
        }
        BOOST_CHECK_EXCEPTION(runtime.finalizeCompositeFieldLayout(first->getId()), NomadException, [](const NomadException& error) {
            const auto message = std::string(error.what());
            return message.find("cycle") != std::string::npos && message.find("FirstInline") != std::string::npos;
        });
        BOOST_TEST(!first->isFieldLayoutFinalized());
        BOOST_TEST(!second->isFieldLayoutFinalized());
        BOOST_CHECK_THROW((void)first->getSize(), NomadException);
        BOOST_CHECK_THROW(runtime.finalizeCompositeFieldLayout(first->getId()), NomadException);
        BOOST_TEST(!first->isFieldLayoutFinalized());
        BOOST_REQUIRE(runtime.registerProperty(first->getId(), "extra", runtime.getIntegerType()).isValid());
    }
}

BOOST_AUTO_TEST_CASE(class_references_and_native_properties_do_not_create_inline_dependencies) {
    Runtime runtime;
    InlineLayoutTestType* point = nullptr;
    InlineLayoutTestType* nativeOnly = nullptr;
    BOOST_REQUIRE(runtime.registerClass<InlineLayoutTestType>(
        "PointInline", ClassMode::Closed, point
    ).isValid());
    BOOST_REQUIRE(runtime.registerClass<InlineLayoutTestType>(
        "NativeInline", ClassMode::Closed, nativeOnly
    ).isValid());
    const auto* owner = createTestType(runtime, "Player", ClassMode::Closed);
    BOOST_REQUIRE(runtime.registerProperty(owner->getId(), "location", point).isValid());
    BOOST_REQUIRE(runtime.registerProperty(point->getId(), "player", owner).isValid());
    auto getterCalls = 0;
    BOOST_REQUIRE(runtime.addProperty(point->getId(), {
        .valueType = nativeOnly,
        .name = "native",
        .getter = [&](VirtualMachine*, RuntimeValue&) { ++getterCalls; }
    }).isValid());

    runtime.finalizeCompositeFieldLayout(owner->getId());
    BOOST_TEST(owner->isFieldLayoutFinalized());
    BOOST_TEST(point->isFieldLayoutFinalized());
    BOOST_TEST(point->getSize() == 1U);
    BOOST_TEST(owner->getFieldStorageSize() == 1U);
    BOOST_TEST(!nativeOnly->isFieldLayoutFinalized());
    BOOST_TEST(getterCalls == 0);

    InlineLayoutTestType* referenceOnly = nullptr;
    BOOST_REQUIRE(runtime.registerClass<InlineLayoutTestType>(
        "ReferenceInline", ClassMode::Closed, referenceOnly
    ).isValid());
    const auto* referencedClass = createTestType(runtime, "Referenced", ClassMode::Closed);
    BOOST_REQUIRE(runtime.registerProperty(referenceOnly->getId(), "reference", referencedClass).isValid());
    runtime.finalizeCompositeFieldLayout(referenceOnly->getId());
    BOOST_TEST(referenceOnly->getSize() == 1U);
    BOOST_TEST(!referencedClass->isFieldLayoutFinalized());
}

BOOST_AUTO_TEST_CASE(finalization_can_retry_unresolved_sizes_without_rolling_back_completed_dependencies) {
    Runtime runtime;
    InlineLayoutTestType* empty = nullptr;
    FieldLayoutTestType* pending = nullptr;
    BOOST_REQUIRE(runtime.registerClass<InlineLayoutTestType>(
        "EmptyInline", ClassMode::Closed, empty
    ).isValid());
    BOOST_REQUIRE(runtime.registerClass<FieldLayoutTestType>(
        "PendingWidth", ClassMode::Closed, pending, std::nullopt
    ).isValid());
    const auto* owner = createTestType(runtime, "Fields", ClassMode::Closed);
    BOOST_REQUIRE(runtime.registerProperty(owner->getId(), "empty", empty).isValid());
    BOOST_REQUIRE(runtime.registerProperty(owner->getId(), "pending", pending).isValid());
    BOOST_CHECK_THROW(runtime.finalizeCompositeFieldLayout(owner->getId()), NomadException);
    BOOST_TEST(!owner->isFieldLayoutFinalized());
    BOOST_TEST(empty->isFieldLayoutFinalized());
    BOOST_TEST(empty->getSize() == 0U);
    BOOST_TEST(pending->getSizeQueryCount() == 1U);
    BOOST_REQUIRE(runtime.registerProperty(owner->getId(), "extra", runtime.getIntegerType()).isValid());

    pending->setSize(3);
    runtime.finalizeCompositeFieldLayout(owner->getId());
    BOOST_TEST(owner->isFieldLayoutFinalized());
    BOOST_TEST(owner->getFieldStorageSize() == 4U);
    BOOST_TEST(pending->getSizeQueryCount() == 2U);
    runtime.finalizeCompositeFieldLayout(owner->getId());
    BOOST_TEST(owner->getFieldStorageSize() == 4U);
    BOOST_TEST(pending->getSizeQueryCount() == 2U);
}

BOOST_AUTO_TEST_CASE(finalization_checks_overflow_without_rejecting_maximum_offsets) {
    Runtime runtime;
    const auto maximumSize = std::numeric_limits<NomadIndex>::max();
    FieldLayoutTestType* maximum = nullptr;
    InlineLayoutTestType* empty = nullptr;
    BOOST_REQUIRE(runtime.registerClass<FieldLayoutTestType>(
        "MaximumWidth", ClassMode::Closed, maximum, maximumSize
    ).isValid());
    BOOST_REQUIRE(runtime.registerClass<InlineLayoutTestType>(
        "EmptyInline", ClassMode::Closed, empty
    ).isValid());
    const auto* owner = createTestType(runtime, "Fields", ClassMode::Closed);
    BOOST_REQUIRE(runtime.registerProperty(owner->getId(), "maximum", maximum).isValid());
    const auto emptyField = runtime.registerProperty(owner->getId(), "empty", empty);
    BOOST_REQUIRE(emptyField.isValid());
    runtime.finalizeCompositeFieldLayout(owner->getId());
    BOOST_TEST(owner->isFieldLayoutFinalized());
    BOOST_TEST(owner->getFieldStorageSize() == maximumSize);
    BOOST_TEST(owner->getFieldOffset(emptyField) == maximumSize);

    const auto* overflow = createTestType(runtime, "Overflow", ClassMode::Closed);
    BOOST_REQUIRE(runtime.registerProperty(overflow->getId(), "maximum", maximum).isValid());
    BOOST_REQUIRE(runtime.registerProperty(overflow->getId(), "extra", runtime.getIntegerType()).isValid());
    BOOST_CHECK_THROW(runtime.finalizeCompositeFieldLayout(overflow->getId()), NomadException);
    BOOST_TEST(!overflow->isFieldLayoutFinalized());
    BOOST_CHECK_THROW(runtime.finalizeCompositeFieldLayout(overflow->getId()), NomadException);
    BOOST_TEST(!overflow->isFieldLayoutFinalized());
}

BOOST_AUTO_TEST_CASE(composite_and_class_views_share_member_ownership_and_layout) {
    Runtime runtime;
    FieldLayoutTestType* owner = nullptr;
    BOOST_REQUIRE(runtime.registerClass<FieldLayoutTestType>(
        "Fields", ClassMode::Closed, owner, NomadIndex{1}
    ).isValid());
    CompositeType* composite = owner;
    BOOST_TEST(composite->asCompositeType() == composite);
    BOOST_TEST(composite->asClassType() == owner);
    BOOST_TEST(composite->getRuntime() == &runtime);
    BOOST_TEST(composite->getId() == owner->getId());
    BOOST_TEST(composite->getTypeName() == "Fields");
    BOOST_TEST(composite->getInitializerName() == "Fields$init");
    BOOST_TEST(!composite->isOpen());
    BOOST_TEST(composite->getPropertyCount() == 0U);
    BOOST_TEST(composite->getFieldStorageSize() == 0U);
    BOOST_TEST(composite->getMethods().empty());
    BOOST_TEST(composite->getConstructors().empty());
    BOOST_TEST((composite->getStorage() == TypeStorage::Object));
    BOOST_TEST(composite->getSize() == 1U);
    BOOST_TEST(composite->needsFree());
    BOOST_TEST(runtime.getInstructionId(composite->getTypeOpCodes()->initResult) ==
        runtime.getInstructionId(OpCodes::op_object_init_r));
    BOOST_TEST(runtime.getInstructionId(composite->getTypeOpCodes()->freeResult) ==
        runtime.getInstructionId(OpCodes::op_object_free_r));

    const auto stored = composite->registerProperty(runtime,
        std::make_unique<Property>(composite, "stored", runtime.getStringType()));
    BOOST_REQUIRE(stored.isValid());
    auto callbackCalls = 0;
    const auto native = runtime.addProperty(owner->getId(), {
        .valueType = runtime.getIntegerType(),
        .name = "native",
        .getter = [&](VirtualMachine*, RuntimeValue&) { ++callbackCalls; }
    });
    const auto method = runtime.addMethod(owner->getId(), {
        .name = "update",
        .parameters = {},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [&](VirtualMachine*) { ++callbackCalls; },
        .documentation = ""
    });
    const auto constructor = runtime.addConstructor(owner->getId(), {
        .parameters = {},
        .function = [&](VirtualMachine*) { ++callbackCalls; },
        .documentation = ""
    });
    BOOST_REQUIRE(native.isValid());
    BOOST_REQUIRE(method.isValid());
    BOOST_REQUIRE(constructor.isValid());
    BOOST_TEST(composite->getProperties() == owner->getProperties());
    BOOST_TEST(composite->getMethods() == owner->getMethods());
    BOOST_TEST(composite->getConstructors() == owner->getConstructors());
    BOOST_REQUIRE(composite->getMethods().size() == 1U);
    BOOST_REQUIRE(composite->getConstructors().size() == 1U);
    BOOST_TEST(composite->getMethods().front() == runtime.getMethod(method));
    BOOST_TEST(composite->getConstructors().front() == runtime.getConstructor(constructor));
    for (const auto id : {stored, native}) {
        const auto* property = composite->getProperty(id);
        BOOST_REQUIRE(property != nullptr);
        BOOST_TEST(property == runtime.getProperty(id));
        BOOST_TEST(property->getOwnerType() == composite);
        BOOST_TEST(property->getClassType() == owner);
    }
    BOOST_TEST(composite->getPropertyCount() == 2U);
    BOOST_TEST(composite->getFieldIndex(stored) == 0U);
    BOOST_TEST(composite->getFieldOffset(stored) == 0U);
    BOOST_TEST(composite->getFieldStorageSize() == 1U);
    BOOST_TEST(composite->getFieldIndex(native) == NOMAD_INVALID_INDEX);
    BOOST_CHECK_THROW((void)composite->getFieldOffset(native), NomadException);
    BOOST_TEST(callbackCalls == 0);

    Property unowned(nullptr, "unowned", runtime.getIntegerType());
    BOOST_TEST(unowned.getOwnerType() == nullptr);
    BOOST_TEST(unowned.getClassType() == nullptr);
}

BOOST_AUTO_TEST_CASE(field_layout_separates_logical_indices_from_physical_slots) {
    Runtime runtime;
    FieldLayoutTestType* emptyType = nullptr;
    FieldLayoutTestType* pointType = nullptr;
    BOOST_REQUIRE(runtime.registerClass<FieldLayoutTestType>(
        "EmptyWidth", ClassMode::Closed, emptyType, NomadIndex{0}
    ).isValid());
    BOOST_REQUIRE(runtime.registerClass<FieldLayoutTestType>(
        "PointWidth", ClassMode::Closed, pointType, NomadIndex{3}
    ).isValid());
    const auto* owner = createTestType(runtime, "Fields", ClassMode::Opened);
    auto callbackCalls = 0;
    const auto nativeBefore = runtime.addProperty(owner->getId(), {
        .valueType = pointType,
        .name = "nativeBefore",
        .getter = [&](VirtualMachine*, RuntimeValue&) { ++callbackCalls; },
        .setter = [&](VirtualMachine*, const RuntimeValue&) { ++callbackCalls; }
    });
    const auto first = runtime.registerProperty(owner->getId(), "first", emptyType);
    const auto point = runtime.registerProperty(owner->getId(), "point", pointType);
    const auto nativeBetween = runtime.addProperty(owner->getId(), {
        .valueType = runtime.getIntegerType(),
        .name = "nativeBetween",
        .getter = [&](VirtualMachine*, RuntimeValue&) { ++callbackCalls; }
    });
    const auto text = runtime.registerProperty(owner->getId(), "text", runtime.getStringType());
    const auto middle = runtime.registerProperty(owner->getId(), "middle", emptyType);
    const auto number = runtime.registerProperty(owner->getId(), "number", runtime.getIntegerType());
    const auto last = runtime.registerProperty(owner->getId(), "last", emptyType);
    const auto nativeAfter = runtime.addProperty(owner->getId(), {
        .valueType = emptyType,
        .name = "nativeAfter",
        .getter = [&](VirtualMachine*, RuntimeValue&) { ++callbackCalls; }
    });
    const auto fields = std::array{first, point, text, middle, number, last};
    const auto offsets = std::array<NomadIndex, 6>{0, 0, 3, 4, 4, 5};
    for (auto index = NomadIndex{0}; index < fields.size(); ++index) {
        BOOST_REQUIRE(fields[index].isValid());
        BOOST_TEST(owner->getFieldIndex(fields[index]) == index);
        BOOST_TEST(owner->getFieldOffset(fields[index]) == offsets[index]);
    }
    for (const auto id : {nativeBefore, nativeBetween, nativeAfter}) {
        BOOST_REQUIRE(id.isValid());
        BOOST_TEST(owner->getFieldIndex(id) == NOMAD_INVALID_INDEX);
        BOOST_CHECK_THROW((void)owner->getFieldOffset(id), NomadException);
    }
    BOOST_TEST(owner->getPropertyCount() == 9U);
    BOOST_TEST(owner->getFieldStorageSize() == 5U);
    BOOST_TEST(owner->getSize() == 1U);

    const auto* pointProperty = owner->getProperty(point);
    const auto appended = runtime.registerProperty(owner->getId(), "appended", pointType);
    BOOST_REQUIRE(appended.isValid());
    BOOST_TEST(owner->getFieldIndex(appended) == 6U);
    BOOST_TEST(owner->getFieldOffset(appended) == 5U);
    BOOST_TEST(owner->getFieldStorageSize() == 8U);
    BOOST_TEST(owner->getPropertyCount() == 10U);
    BOOST_TEST(owner->getProperty("point")->getId() == point);
    BOOST_TEST(owner->getProperty(point) == pointProperty);
    for (auto index = NomadIndex{0}; index < fields.size(); ++index) {
        BOOST_TEST(owner->getFieldIndex(fields[index]) == index);
        BOOST_TEST(owner->getFieldOffset(fields[index]) == offsets[index]);
    }
    BOOST_TEST(callbackCalls == 0);
}

BOOST_AUTO_TEST_CASE(empty_and_native_only_field_layouts_use_no_storage) {
    Runtime runtime;
    const auto* owner = createTestType(runtime, "Fields", ClassMode::Closed);
    BOOST_TEST(owner->getPropertyCount() == 0U);
    BOOST_TEST(owner->getFieldStorageSize() == 0U);
    BOOST_TEST(owner->getSize() == 1U);
    auto getterCalls = 0;
    const auto native = runtime.addProperty(owner->getId(), {
        .valueType = runtime.getIntegerType(),
        .name = "native",
        .getter = [&](VirtualMachine*, RuntimeValue&) { ++getterCalls; }
    });
    BOOST_REQUIRE(native.isValid());
    BOOST_TEST(owner->getPropertyCount() == 1U);
    BOOST_TEST(owner->getFieldStorageSize() == 0U);
    BOOST_TEST(owner->getSize() == 1U);
    BOOST_CHECK_THROW((void)owner->getFieldOffset(native), NomadException);
    BOOST_TEST(getterCalls == 0);
}

BOOST_AUTO_TEST_CASE(field_layout_queries_follow_size_resolution) {
    Runtime runtime;
    FieldLayoutTestType* pendingType = nullptr;
    BOOST_REQUIRE(runtime.registerClass<FieldLayoutTestType>(
        "PendingWidth", ClassMode::Closed, pendingType, std::nullopt
    ).isValid());
    const auto* owner = createTestType(runtime, "Fields", ClassMode::Closed);
    const auto first = runtime.registerProperty(owner->getId(), "first", runtime.getIntegerType());
    const auto pending = runtime.registerProperty(owner->getId(), "pending", pendingType);
    const auto following = runtime.registerProperty(owner->getId(), "following", runtime.getFloatType());
    const auto native = runtime.addProperty(owner->getId(), {
        .valueType = pendingType,
        .name = "native",
        .getter = [](VirtualMachine*, RuntimeValue&) {}
    });
    BOOST_REQUIRE(first.isValid());
    BOOST_REQUIRE(pending.isValid());
    BOOST_REQUIRE(following.isValid());
    BOOST_REQUIRE(native.isValid());
    BOOST_TEST(owner->getFieldOffset(first) == 0U);
    BOOST_TEST(owner->getFieldOffset(pending) == 1U);
    BOOST_CHECK_THROW((void)owner->getFieldOffset(following), NomadException);
    BOOST_CHECK_THROW((void)owner->getFieldStorageSize(), NomadException);
    for (const auto id : {native, PropertyId::INVALID, PropertyId{NOMAD_ID_MAX}}) {
        BOOST_CHECK_EXCEPTION((void)owner->getFieldOffset(id), NomadException, [](const NomadException& error) {
            return std::string(error.what()).find("property ID does not identify a stored field") != std::string::npos;
        });
    }

    pendingType->setSize(0);
    BOOST_TEST(owner->getFieldOffset(following) == 1U);
    BOOST_TEST(owner->getFieldStorageSize() == 2U);
    pendingType->setSize(3);
    BOOST_TEST(owner->getFieldOffset(following) == 4U);
    BOOST_TEST(owner->getFieldStorageSize() == 5U);
    BOOST_TEST(owner->getFieldIndex(following) == 2U);
    BOOST_TEST(owner->getProperty("pending")->getId() == pending);
}

BOOST_AUTO_TEST_CASE(field_layout_checks_overflow_without_rejecting_representable_sizes) {
    Runtime runtime;
    const auto maximumSize = std::numeric_limits<NomadIndex>::max();
    FieldLayoutTestType* maximumType = nullptr;
    FieldLayoutTestType* emptyType = nullptr;
    BOOST_REQUIRE(runtime.registerClass<FieldLayoutTestType>(
        "MaximumWidth", ClassMode::Closed, maximumType, maximumSize
    ).isValid());
    BOOST_REQUIRE(runtime.registerClass<FieldLayoutTestType>(
        "EmptyWidth", ClassMode::Closed, emptyType, NomadIndex{0}
    ).isValid());
    const auto* owner = createTestType(runtime, "Fields", ClassMode::Closed);
    const auto maximum = runtime.registerProperty(owner->getId(), "maximum", maximumType);
    const auto empty = runtime.registerProperty(owner->getId(), "empty", emptyType);
    BOOST_REQUIRE(maximum.isValid());
    BOOST_REQUIRE(empty.isValid());
    BOOST_TEST(owner->getFieldOffset(maximum) == 0U);
    BOOST_TEST(owner->getFieldOffset(empty) == maximumSize);
    BOOST_TEST(owner->getFieldStorageSize() == maximumSize);

    const auto overflow = runtime.registerProperty(owner->getId(), "overflow", runtime.getIntegerType());
    BOOST_REQUIRE(overflow.isValid());
    BOOST_TEST(owner->getFieldOffset(overflow) == maximumSize);
    BOOST_CHECK_THROW((void)owner->getFieldStorageSize(), NomadException);
    const auto following = runtime.registerProperty(owner->getId(), "following", runtime.getIntegerType());
    BOOST_REQUIRE(following.isValid());
    BOOST_CHECK_THROW((void)owner->getFieldOffset(following), NomadException);
}

BOOST_AUTO_TEST_CASE(field_offsets_reject_invalid_and_foreign_property_ids) {
    Runtime runtime;
    const auto* owner = createTestType(runtime, "Fields", ClassMode::Closed);
    const auto* other = createTestType(runtime, "Other", ClassMode::Closed);
    BOOST_CHECK_THROW((void)owner->getFieldOffset(PropertyId::INVALID), NomadException);
    const auto first = runtime.registerProperty(owner->getId(), "first", runtime.getIntegerType());
    const auto foreign = runtime.registerProperty(other->getId(), "foreign", runtime.getIntegerType());
    BOOST_REQUIRE(first.isValid());
    BOOST_REQUIRE(foreign.isValid());
    BOOST_TEST(owner->getFieldOffset(first) == 0U);
    for (const auto id : {PropertyId::INVALID, PropertyId{-2}, PropertyId{NOMAD_ID_MAX - 1},
        PropertyId{NOMAD_ID_MAX}, foreign}) {
        BOOST_CHECK_THROW((void)owner->getFieldOffset(id), NomadException);
        BOOST_TEST(owner->getFieldIndex(id) == NOMAD_INVALID_INDEX);
    }
}

BOOST_AUTO_TEST_CASE(class_conversion_returns_the_original_descriptor_for_classes_and_derived_types) {
    Runtime runtime;
    const Type* opened = createTestType(runtime, "Opened", ClassMode::Opened);
    const Type* closed = createTestType(runtime, "Closed", ClassMode::Closed);
    BOOST_TEST(opened->asClassType() == opened);
    BOOST_TEST(closed->asClassType() == closed);
    BOOST_TEST(opened->asCompositeType() == opened);
    BOOST_TEST(closed->asCompositeType() == closed);
    BOOST_TEST(opened->asCompositeType()->isOpen());
    BOOST_TEST(!closed->asCompositeType()->isOpen());
    const Type* stringType = runtime.getStringType();
    BOOST_TEST(stringType->asClassType() == runtime.getStringType());
    BOOST_TEST(stringType->asCompositeType() == runtime.getStringType());
    const auto* fileNameType = runtime.getFileNameType();
    BOOST_TEST(fileNameType->asClassType() == fileNameType);
    BOOST_TEST(fileNameType->asCompositeType() == fileNameType);
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
            BOOST_TEST(type->asCompositeType() == nullptr);
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
        .valueType = runtime.getIntegerType(),
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
        .valueType = runtime.getIntegerType(),
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
        .valueType = runtime.getIntegerType(),
        .name = "x",
        .getter = [](VirtualMachine*, RuntimeValue&) {},
        .setter = {},
        .documentation = ""
    }).isInvalid());
    const auto nativeId = runtime.addProperty(typeId, {
        .valueType = runtime.getIntegerType(),
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
            .valueType = runtime.getIntegerType(),
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
