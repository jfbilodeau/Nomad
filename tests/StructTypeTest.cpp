// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/StructType.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <type_traits>
#include <utility>
#include <vector>

using namespace nomad;

static_assert(!std::is_abstract_v<StructType>);
static_assert(std::is_base_of_v<CompositeType, StructType>);
static_assert(!std::is_base_of_v<ClassType, StructType>);

namespace {

class FailingFieldType final : public ClassType {
public:
    FailingFieldType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        FailingFieldType*& descriptor
    ):
        ClassType(runtime, id, std::move(name), mode)
    {
        descriptor = this;
    }

    void initValue(RuntimeValue* value) const override {
        value->setStringValue("partial");
        if (++m_initializationCount == failInitializationAt) {
            throw NomadException("Field initialization failed");
        }
    }

    void copyValue(const RuntimeValue* source, RuntimeValue* destination) const override {
        ClassType::copyValue(source, destination);
        if (++m_copyCount == failCopyAt) {
            throw NomadException("Field copy failed");
        }
    }

    NomadIndex failInitializationAt = 0;
    NomadIndex failCopyAt = 0;

private:
    mutable NomadIndex m_initializationCount = 0;
    mutable NomadIndex m_copyCount = 0;
};

} // namespace

BOOST_AUTO_TEST_SUITE(struct_type)

BOOST_AUTO_TEST_CASE(registration_identity_and_unresolved_layout_contracts) {
    Runtime runtime;
    const auto id = runtime.registerStruct("Point", "A point.");
    BOOST_REQUIRE(id.isValid());
    const auto* type = runtime.getTypeByName("Point")->asStructType();
    BOOST_REQUIRE(type != nullptr);
    BOOST_TEST(type->getId() == id);
    BOOST_TEST(type->getDocumentation() == "A point.");
    BOOST_TEST(type->getInitializerName() == "Point$init");
    BOOST_TEST(!type->isOpen());
    BOOST_TEST(!type->isVoid());
    BOOST_TEST(type->asClassType() == nullptr);
    BOOST_TEST((type->getStorage() == TypeStorage::Struct));
    BOOST_TEST(type->sameType(type));
    BOOST_TEST(!type->sameType(nullptr));
    Runtime otherRuntime;
    const auto otherId = otherRuntime.registerStruct("Point");
    BOOST_REQUIRE(otherId.isValid());
    BOOST_TEST(!type->sameType(otherRuntime.getTypeByName("Point")));
    BOOST_TEST(runtime.getIntegerType()->asStructType() == nullptr);
    BOOST_CHECK_THROW((void)type->getSize(), NomadException);
    BOOST_CHECK_THROW((void)type->needsFree(), NomadException);
    BOOST_CHECK_THROW((void)type->getAssignmentScratchSize(), NomadException);
    BOOST_CHECK_THROW(type->initValue(nullptr), NomadException);
    BOOST_CHECK_THROW(type->copyValue(nullptr, nullptr), NomadException);
    BOOST_CHECK_THROW(type->freeValue(nullptr), NomadException);
    BOOST_TEST(runtime.registerStruct("Point").isInvalid());
    BOOST_TEST(runtime.registerStruct("").isInvalid());
    BOOST_TEST(runtime.registerStruct("int").isInvalid());
    BOOST_TEST(runtime.registerClass("Point", ClassMode::Closed).isInvalid());
    BOOST_TEST(runtime.registerProperty(runtime.getIntegerType()->getId(), "x", type).isInvalid());
    BOOST_TEST(runtime.registerProperty(id, "foreign", otherRuntime.getIntegerType()).isInvalid());
}

BOOST_AUTO_TEST_CASE(empty_and_nested_scalar_layouts_remain_distinct_from_void) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("Empty");
    const auto pointId = runtime.registerStruct("Point");
    const auto pairId = runtime.registerStruct("Pair");
    const auto* empty = runtime.getTypeByName("Empty")->asStructType();
    const auto* point = runtime.getTypeByName("Point")->asStructType();
    const auto* pair = runtime.getTypeByName("Pair")->asStructType();
    BOOST_REQUIRE(empty != nullptr && point != nullptr && pair != nullptr);
    const auto x = runtime.registerProperty(pointId, "x", runtime.getFloatType());
    const auto marker = runtime.registerProperty(pointId, "marker", empty);
    const auto y = runtime.registerProperty(pointId, "y", runtime.getFloatType());
    BOOST_REQUIRE(x.isValid() && marker.isValid() && y.isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "first", point).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "second", point).isValid());
    runtime.finalizeCompositeFieldLayout(pairId);
    BOOST_TEST(empty->isFieldLayoutFinalized());
    BOOST_TEST(empty->getSize() == 0U);
    BOOST_TEST(!empty->needsFree());
    BOOST_TEST(!empty->sameType(runtime.getVoidType()));
    empty->initValue(nullptr);
    empty->copyValue(nullptr, nullptr);
    empty->freeValue(nullptr);
    empty->assignValue(nullptr, nullptr);
    BOOST_TEST(point->getSize() == 2U);
    BOOST_TEST(point->getFieldOffset(marker) == 1U);
    BOOST_TEST(point->getFieldOffset(y) == 1U);
    BOOST_TEST(pair->getSize() == 4U);
    BOOST_TEST(!pair->needsFree());
    std::array<RuntimeValue, 4> values{};
    std::array<RuntimeValue, 4> copy{};
    std::array<RuntimeValue, 4> scratch{};
    pair->initValue(values.data());
    values[0].setFloatValue(3.0f);
    values[3].setFloatValue(7.0f);
    pair->copyValue(values.data(), copy.data());
    copy[0].setFloatValue(11.0f);
    BOOST_TEST(values[0].getFloatValue() == 3.0f);
    BOOST_TEST(copy[3].getFloatValue() == 7.0f);
    pair->assignValue(copy.data(), values.data(), scratch.data());
    BOOST_TEST(values[0].getFloatValue() == 11.0f);
    pair->freeValue(values.data());
    pair->freeValue(copy.data());
    BOOST_TEST(runtime.registerProperty(pointId, "z", runtime.getFloatType()).isInvalid());
    BOOST_TEST(runtime.registerProperty(pointId, "x", runtime.getFloatType()) == x);
}

BOOST_AUTO_TEST_CASE(nested_owned_fields_copy_and_release_references_without_construction) {
    Runtime runtime;
    const auto payloadId = runtime.registerStruct("Payload");
    const auto wrapperId = runtime.registerStruct("Wrapper");
    const auto* payload = runtime.getTypeByName("Payload")->asStructType();
    const auto* wrapper = runtime.getTypeByName("Wrapper")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(payloadId, "name", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(payloadId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(wrapperId, "payload", payload).isValid());
    BOOST_REQUIRE(runtime.registerProperty(wrapperId, "label", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(wrapperId);
    BOOST_TEST(wrapper->needsFree());
    BOOST_TEST(wrapper->getSize() == 3U);
    std::array<RuntimeValue, 3> source{};
    std::array<RuntimeValue, 3> destination{};
    std::array<RuntimeValue, 3> scratch{};
    wrapper->initValue(source.data());
    source[0].setStringValue("source");
    source[1].setIntegerValue(17);
    source[2].setStringValue("label");
    wrapper->copyValue(source.data(), destination.data());
    BOOST_TEST(source[0].getObjectValue() == destination[0].getObjectValue());
    BOOST_TEST(source[0].getObjectValue()->getReferenceCount() == 2U);
    wrapper->copyValue(destination.data(), destination.data());
    BOOST_TEST(source[0].getObjectValue()->getReferenceCount() == 2U);
    destination[1].setIntegerValue(19);
    BOOST_TEST(source[1].getIntegerValue() == 17);
    wrapper->assignValue(source.data(), destination.data(), scratch.data());
    BOOST_TEST(source[0].getObjectValue()->getReferenceCount() == 2U);
    BOOST_TEST(destination[1].getIntegerValue() == 17);
    wrapper->freeValue(source.data());
    BOOST_TEST(destination[0].getObjectValue()->getReferenceCount() == 1U);
    wrapper->freeValue(destination.data());
    BOOST_TEST(source[0].getObjectValue() == nullptr);
    BOOST_TEST(destination[2].getObjectValue() == nullptr);
}

BOOST_AUTO_TEST_CASE(partially_initialized_and_copied_nested_storage_remains_releasable) {
    Runtime runtime;
    FailingFieldType* field = nullptr;
    BOOST_REQUIRE(runtime.registerClass<FailingFieldType>(
        "Failure", ClassMode::Closed, field
    ).isValid());
    const auto innerId = runtime.registerStruct("Inner");
    const auto outerId = runtime.registerStruct("Outer");
    const auto* inner = runtime.getTypeByName("Inner")->asStructType();
    const auto* outer = runtime.getTypeByName("Outer")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(innerId, "first", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "second", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "before", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "inner", inner).isValid());
    runtime.finalizeCompositeFieldLayout(outerId);
    std::array<RuntimeValue, 3> source{};
    std::array<RuntimeValue, 3> destination{};
    std::array<RuntimeValue, 3> scratch{};
    field->failInitializationAt = 2;
    BOOST_CHECK_THROW(outer->initValue(source.data()), NomadException);
    outer->freeValue(source.data());
    BOOST_TEST(source[0].getObjectValue() == nullptr);
    BOOST_TEST(source[1].getObjectValue() == nullptr);
    field->failInitializationAt = 0;
    outer->initValue(source.data());
    outer->initValue(destination.data());
    const auto previous = destination;
    field->failCopyAt = 2;
    BOOST_CHECK_THROW(outer->assignValue(source.data(), destination.data(), scratch.data()), NomadException);
    BOOST_TEST(destination[0].getObjectValue() == previous[0].getObjectValue());
    BOOST_TEST(source[0].getObjectValue()->getReferenceCount() == 1U);
    BOOST_TEST(source[1].getObjectValue()->getReferenceCount() == 1U);
    BOOST_TEST(scratch[0].getObjectValue() == nullptr);
    BOOST_TEST(scratch[1].getObjectValue() == nullptr);
    field->failCopyAt = 0;
    outer->assignValue(source.data(), destination.data(), scratch.data());
    outer->freeValue(source.data());
    outer->freeValue(destination.data());
}

BOOST_AUTO_TEST_CASE(inline_cycles_fail_but_reference_cycles_do_not) {
    Runtime runtime;
    const auto firstId = runtime.registerStruct("First");
    const auto secondId = runtime.registerStruct("Second");
    const auto* first = runtime.getTypeByName("First")->asStructType();
    const auto* second = runtime.getTypeByName("Second")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(firstId, "second", second).isValid());
    BOOST_REQUIRE(runtime.registerProperty(secondId, "first", first).isValid());
    BOOST_CHECK_THROW(runtime.finalizeCompositeFieldLayout(firstId), NomadException);
    BOOST_TEST(!first->isFieldLayoutFinalized());
    BOOST_TEST(!second->isFieldLayoutFinalized());
    const auto recordId = runtime.registerStruct("Record");
    const auto ownerId = runtime.registerClass("Owner", ClassMode::Closed);
    const auto* record = runtime.getTypeByName("Record")->asStructType();
    const auto* owner = runtime.getTypeByName("Owner");
    BOOST_REQUIRE(runtime.registerProperty(recordId, "owner", owner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "record", record).isValid());
    runtime.finalizeCompositeFieldLayout(recordId);
    BOOST_TEST(record->getSize() == 1U);
    BOOST_TEST(record->needsFree());
    BOOST_TEST(!owner->asCompositeType()->isFieldLayoutFinalized());
}

BOOST_AUTO_TEST_CASE(every_emission_operation_rejects_scalar_fallback_without_writing_instructions) {
    Runtime runtime;
    const auto id = runtime.registerStruct("Empty");
    const Type* type = runtime.getTypeByName("Empty");
    runtime.finalizeCompositeFieldLayout(id);
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    const auto initialSize = instructions.size();
    BOOST_CHECK_THROW(type->emitInitResult(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitInitIntermediate(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitFreeResult(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitFreeIntermediate(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitFreeStackValue(compiler, 0), NomadException);
    BOOST_CHECK_THROW(type->emitFreeFunctionVariable(compiler, 0), NomadException);
    BOOST_CHECK_THROW(type->emitFreeParameter(compiler, 0), NomadException);
    for (const auto target : {ExpressionTarget::Result, ExpressionTarget::Intermediate, ExpressionTarget::Stack}) {
        BOOST_CHECK_THROW(type->emitLoadFunctionVariable(compiler, 0, target), NomadException);
        BOOST_CHECK_THROW(type->emitLoadParameter(compiler, 0, target), NomadException);
        BOOST_CHECK_THROW(type->emitLoadDynamicVariable(compiler, 0, target), NomadException);
        BOOST_CHECK_THROW(type->emitLoadContextVariable(compiler, 0, target), NomadException);
    }
    BOOST_CHECK_THROW(type->emitCopyResultToStack(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitMoveResultToIntermediate(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitMoveIntermediateToResult(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitMoveResultToStack(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitPushIntermediate(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitPopResult(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitPopIntermediate(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitStoreParameter(compiler, 0), NomadException);
    BOOST_CHECK_THROW(type->emitStoreFunctionVariable(compiler, 0), NomadException);
    BOOST_CHECK_THROW(type->emitStoreDynamicVariable(compiler, 0), NomadException);
    BOOST_CHECK_THROW(type->emitStoreContextVariable(compiler, 0), NomadException);
    BOOST_TEST(instructions.size() == initialSize);
    RuntimeValue unused;
    BOOST_TEST(type->toString(unused) == "Empty");
}

BOOST_AUTO_TEST_SUITE_END()
