// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/TypedValue.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace nomad;

static_assert(std::is_nothrow_move_constructible_v<TypedValue>);
static_assert(std::is_nothrow_move_assignable_v<TypedValue>);

namespace {

class ThrowingCopyType final : public ClassType {
public:
    using ClassType::ClassType;

    void copyValue(const RuntimeValue* source, RuntimeValue* destination) const override {
        ClassType::copyValue(source, destination);
        throw NomadException("Typed value copy failed");
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(typed_values)

BOOST_AUTO_TEST_CASE(scalars_copy_and_move_owned_references_without_array_storage) {
    Runtime runtime;
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        RuntimeValue source{NomadString{"scalar"}};
        {
            TypedValue value(runtime.getStringType(), source);
            BOOST_REQUIRE_EQUAL(value.getStorage().size(), 1U);
            BOOST_TEST(value.getStorage().data() == &value.getValue());
            BOOST_TEST(source.getObjectValue()->getReferenceCount() == 2U);
            TypedValue copy(value);
            BOOST_TEST(source.getObjectValue()->getReferenceCount() == 3U);
            TypedValue moved(std::move(copy));
            BOOST_TEST(copy.getType() == nullptr);
            BOOST_TEST(copy.getStorage().empty());
            BOOST_TEST(moved.getValue().getStringValue() == "scalar");
            BOOST_TEST(source.getObjectValue()->getReferenceCount() == 3U);
            value = moved;
            BOOST_TEST(source.getObjectValue()->getReferenceCount() == 3U);
            const auto* alias = &value;
            value = *alias;
            BOOST_TEST(source.getObjectValue()->getReferenceCount() == 3U);
            moved = std::move(value);
            BOOST_TEST(value.getType() == nullptr);
            BOOST_TEST(source.getObjectValue()->getReferenceCount() == 2U);
        }
        BOOST_TEST(source.getObjectValue()->getReferenceCount() == 1U);
        source.freeObjectValue();
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(storage_transitions_preserve_empty_types_and_single_slot_struct_addresses) {
    Runtime runtime;
    const auto singleId = runtime.registerStruct("SingleSlot");
    const auto emptyId = runtime.registerStruct("EmptySlots");
    const auto* single = runtime.getTypeByName("SingleSlot");
    const auto* empty = runtime.getTypeByName("EmptySlots");
    BOOST_REQUIRE(runtime.registerProperty(singleId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(singleId);
    runtime.finalizeCompositeFieldLayout(emptyId);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        std::array<RuntimeValue, 1> source{RuntimeValue{NomadString{"field"}}};
        {
            TypedValue scalar(runtime.getIntegerType(), RuntimeValue{NomadInteger{7}});
            TypedValue aggregate(single, source);
            const auto* address = aggregate.getStorage().data();
            BOOST_CHECK_THROW(static_cast<void>(aggregate.getValue()), std::invalid_argument);
            BOOST_CHECK_THROW((TypedValue{single, source[0]}), std::invalid_argument);
            scalar = std::move(aggregate);
            BOOST_TEST(scalar.getType() == single);
            BOOST_TEST(scalar.getStorage().data() == address);
            BOOST_TEST(aggregate.getType() == nullptr);
            BOOST_TEST(aggregate.getStorage().empty());
            aggregate = TypedValue(runtime.getIntegerType(), RuntimeValue{NomadInteger{11}});
            scalar = aggregate;
            BOOST_TEST(scalar.getValue().getIntegerValue() == 11);
            BOOST_TEST(source[0].getObjectValue()->getReferenceCount() == 1U);
            TypedValue value(single, source);
            scalar = value;
            BOOST_TEST(scalar.getStorage().data() != value.getStorage().data());
            BOOST_TEST(scalar.getStorage()[0].getStringValue() == "field");
            scalar = TypedValue(empty, std::span<const RuntimeValue>{});
            BOOST_TEST(scalar.getType() == empty);
            BOOST_TEST(scalar.getStorage().empty());
            BOOST_TEST(scalar.getStorage().data() == nullptr);
            BOOST_CHECK_THROW(static_cast<void>(scalar.getValue()), std::invalid_argument);
            value = scalar;
            BOOST_TEST(source[0].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(value.getType() == empty);
            TypedValue emptyMove(std::move(value));
            BOOST_TEST(emptyMove.getType() == empty);
            BOOST_TEST(emptyMove.getStorage().empty());
            value = TypedValue(runtime.getVoidType(), RuntimeValue{});
            scalar = value;
            BOOST_TEST(scalar.getType()->isVoid());
            BOOST_TEST(scalar.getStorage().empty());
            BOOST_TEST(scalar.getStorage().data() == nullptr);
            scalar = TypedValue(single, source);
            BOOST_TEST(scalar.getStorage()[0].getStringValue() == "field");
            scalar = TypedValue(runtime.getBooleanType(), RuntimeValue{true});
            BOOST_TEST(scalar.getValue().getBooleanValue());
        }
        BOOST_TEST(source[0].getObjectValue()->getReferenceCount() == 1U);
        single->freeValue(source.data());
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(failed_scalar_and_array_copies_release_partial_ownership) {
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    const auto fieldId = manager.registerClass<ThrowingCopyType>("ThrowingCopy", ClassMode::Closed);
    const auto* field = runtime.getTypeByName("ThrowingCopy");
    BOOST_REQUIRE(fieldId.isValid());
    const auto aggregateId = runtime.registerStruct("FailingAggregateCopy");
    BOOST_REQUIRE(runtime.registerProperty(aggregateId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(aggregateId, "failing", field).isValid());
    runtime.finalizeCompositeFieldLayout(aggregateId);
    const auto* aggregate = runtime.getTypeByName("FailingAggregateCopy");
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        std::array<RuntimeValue, 2> source{
            RuntimeValue{NomadString{"first"}},
            RuntimeValue{NomadString{"second"}}
        };
        BOOST_CHECK_THROW((TypedValue{field, source[1]}), NomadException);
        BOOST_TEST(source[1].getObjectValue()->getReferenceCount() == 1U);
        BOOST_CHECK_THROW((TypedValue{aggregate, source}), NomadException);
        BOOST_TEST(source[0].getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(source[1].getObjectValue()->getReferenceCount() == 1U);
        aggregate->freeValue(source.data());
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_SUITE_END()
