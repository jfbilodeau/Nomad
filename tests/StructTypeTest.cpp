// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerException.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/StructFieldInitializerStatement.hpp>
#include <nomad/compiler/StructMemberDeclaration.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/InstructionDump.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/InterpreterContext.hpp>
#include <nomad/script/InterpreterValue.hpp>
#include <nomad/script/NativeCallable.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

using namespace nomad;

static_assert(!std::is_abstract_v<StructType>);
static_assert(std::is_base_of_v<CompositeType, StructType>);
static_assert(!std::is_base_of_v<ClassType, StructType>);
static_assert(noexcept(std::declval<const Type&>().freeValue(nullptr)));
static_assert(noexcept(std::declval<const ClassType&>().freeValue(nullptr)));
static_assert(noexcept(std::declval<const StructType&>().freeValue(nullptr)));
static_assert(noexcept(std::declval<RuntimeValue&>().freeObjectValue()));
static_assert(!noexcept(std::declval<const Type&>().initValue(nullptr)));
static_assert(!noexcept(std::declval<const Type&>().copyValue(nullptr, nullptr)));
static_assert(!noexcept(std::declval<const Type&>().assignValue(nullptr, nullptr)));

namespace {

class CleanupLayoutProbeType final : public ClassType {
public:
    CleanupLayoutProbeType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        CleanupLayoutProbeType*& descriptor
    ):
        ClassType(runtime, id, std::move(name), mode)
    {
        descriptor = this;
    }

    [[nodiscard]] NomadIndex getSize() const override {
        requireQueriesAllowed();
        return ClassType::getSize();
    }

    [[nodiscard]] bool needsFree() const override {
        requireQueriesAllowed();
        return ClassType::needsFree();
    }

    bool rejectQueries = false;

private:
    void requireQueriesAllowed() const {
        if (rejectQueries) {
            throw NomadException("Cleanup must not query field metadata");
        }
    }
};

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

BOOST_AUTO_TEST_CASE(finalized_cleanup_uses_cached_nested_layouts_without_field_queries) {
    Runtime runtime;
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    CleanupLayoutProbeType* field = nullptr;
    const auto fieldId = runtime.registerClass<CleanupLayoutProbeType>("CleanupProbe", ClassMode::Closed, field);
    BOOST_REQUIRE(fieldId.isValid());
    const auto emptyId = runtime.registerStruct("CleanupEmpty");
    const auto innerId = runtime.registerStruct("CleanupInner");
    const auto outerId = runtime.registerStruct("CleanupOuter");
    const auto* empty = runtime.getTypeByName("CleanupEmpty")->asStructType();
    const auto* inner = runtime.getTypeByName("CleanupInner")->asStructType();
    const auto* outer = runtime.getTypeByName("CleanupOuter")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(innerId, "reference", field).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "empty", empty).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "inner", inner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(outerId);

    const auto fields = outer->getFieldLayouts();
    BOOST_REQUIRE(fields.size() == 4U);
    BOOST_TEST(outer->getLayoutSize() == 4U);
    BOOST_TEST(fields[0].type == runtime.getIntegerType());
    BOOST_TEST(fields[0].offset == 0U);
    BOOST_TEST(fields[0].size == 1U);
    BOOST_TEST(fields[1].type == empty);
    BOOST_TEST(fields[1].offset == 1U);
    BOOST_TEST(fields[1].size == 0U);
    BOOST_TEST(fields[2].type == inner);
    BOOST_TEST(fields[2].offset == 1U);
    BOOST_TEST(fields[2].size == 2U);
    BOOST_TEST(fields[3].type == runtime.getStringType());
    BOOST_TEST(fields[3].offset == 3U);
    BOOST_TEST(fields[3].size == 1U);
    BOOST_TEST(empty->getFieldLayouts().empty());
    BOOST_TEST(empty->getLayoutSize() == 0U);
    empty->freeValue(nullptr);
    runtime.finalizeCompositeFieldLayout(outerId);
    BOOST_TEST(outer->getFieldLayouts().data() == fields.data());

    for (auto index = 0; index < 3; ++index) {
        std::array<RuntimeValue, 4> values{};
        values[0].setIntegerValue(42);
        values[1].setObjectValue(new NomadObject());
        values[2].setStringValue("inner");
        values[3].setStringValue("outer");
        {
            InterpreterValue copy(outer, values);
            field->rejectQueries = true;
            BOOST_CHECK_THROW((void)field->getSize(), NomadException);
            BOOST_CHECK_THROW((void)field->needsFree(), NomadException);
            outer->freeValue(values.data());
            BOOST_TEST(values[1].getObjectValue() == nullptr);
            BOOST_TEST(values[2].getObjectValue() == nullptr);
            BOOST_TEST(values[3].getObjectValue() == nullptr);
            BOOST_TEST(copy.getStorage()[1].getObjectValue()->getReferenceCount() == 1U);
        }
        field->rejectQueries = false;
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(type_category_predicates_distinguish_structs_classes_and_other_types) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("EmptyCategory");
    const auto nonemptyId = runtime.registerStruct("NonemptyCategory");
    BOOST_REQUIRE(emptyId.isValid() && nonemptyId.isValid());
    const auto* empty = runtime.getTypeByName("EmptyCategory");
    const auto* nonempty = runtime.getTypeByName("NonemptyCategory");
    BOOST_REQUIRE(runtime.registerProperty(nonemptyId, "value", runtime.getIntegerType()).isValid());
    for (const auto* type : {empty, nonempty}) {
        BOOST_TEST(type->isStruct());
        BOOST_TEST(!type->isClass());
        BOOST_TEST(type->isComposite());
        BOOST_TEST(!type->isVoid());
        BOOST_TEST(!type->asCompositeType()->isFieldLayoutFinalized());
        runtime.finalizeCompositeFieldLayout(type->getId());
        BOOST_TEST(type->isStruct());
        BOOST_TEST(!type->isClass());
        BOOST_TEST(type->isComposite());
    }
    BOOST_TEST(empty->getSize() == 0U);
    BOOST_TEST(nonempty->getSize() == 1U);

    BOOST_REQUIRE(runtime.registerClass("ClosedCategory", ClassMode::Closed).isValid());
    BOOST_REQUIRE(runtime.registerClass("OpenedCategory", ClassMode::Opened).isValid());
    for (const auto* type : std::vector<const Type*>{
        runtime.getTypeByName("ClosedCategory"), runtime.getTypeByName("OpenedCategory"),
        runtime.getStringType(), runtime.getFileNameType()
    }) {
        BOOST_TEST_CONTEXT(type->getTypeName()) {
            BOOST_TEST(!type->isStruct());
            BOOST_TEST(type->isClass());
            BOOST_TEST(type->isComposite());
        }
    }

    const auto enumId = runtime.createEnum("CategoryState", {{"Ready", 1}});
    BOOST_REQUIRE(enumId.isValid());
    for (const auto* type : std::vector<const Type*>{
        runtime.getVoidType(), runtime.getIntegerType(), runtime.getFloatType(), runtime.getBooleanType(),
        runtime.getType(enumId).value(),
        runtime.getCallbackType({runtime.getIntegerType()}, runtime.getIntegerType())
    }) {
        BOOST_REQUIRE(type != nullptr);
        BOOST_TEST_CONTEXT(type->getTypeName()) {
            BOOST_TEST(!type->isStruct());
            BOOST_TEST(!type->isClass());
            BOOST_TEST(!type->isComposite());
        }
    }
}

BOOST_AUTO_TEST_CASE(object_aggregate_assignment_cleans_partial_scratch_and_preserves_destination) {
    const auto before = RuntimeValue::getLiveStringCount();
    Runtime runtime;
    auto& manager = runtime.getTypeManager();
    FailingFieldType* field = nullptr;
    const auto fieldId = manager.registerClass<FailingFieldType>("Failing", ClassMode::Closed, field);
    const auto aggregateId = manager.registerStruct("PartialAggregate");
    const auto* aggregate = manager.getTypeByName("PartialAggregate")->asStructType();
    BOOST_REQUIRE(manager.registerScriptStructProperty(aggregateId, "first", manager.getType(fieldId).value()).isValid());
    BOOST_REQUIRE(manager.registerScriptStructProperty(aggregateId, "second", manager.getType(fieldId).value()).isValid());
    const auto ownerId = manager.registerClass("PartialAggregateOwner", ClassMode::Closed);
    const auto* owner = manager.getTypeByName("PartialAggregateOwner")->asClassType();
    BOOST_REQUIRE(manager.registerScriptClassProperty(ownerId, "value", aggregate).isValid());
    manager.finalizeCompositeFieldLayout(ownerId);
    {
        ScriptObject object(owner);
        std::array<RuntimeValue, 2> source{};
        std::array<RuntimeValue, 2> scratch{};
        source[0].setStringValue("first");
        source[1].setStringValue("second");
        field->failCopyAt = 2;
        BOOST_CHECK_THROW(object.setFieldStorage(0, source, scratch), NomadException);
        BOOST_TEST(source[0].getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(source[1].getObjectValue()->getReferenceCount() == 1U);
        for (const auto& value : object.getFieldStorage(0)) {
            BOOST_TEST(value.getObjectValue() == nullptr);
        }
        for (const auto& value : scratch) {
            BOOST_TEST(value.getObjectValue() == nullptr);
        }
        field->failCopyAt = 0;
        object.setFieldStorage(0, source, scratch);
        aggregate->freeValue(source.data());
        BOOST_TEST(object.getFieldStorage(0)[0].getObjectValue()->getReferenceCount() == 1U);
    }
    BOOST_TEST(RuntimeValue::getLiveStringCount() == before);
}

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
    BOOST_TEST(noexcept(type->freeValue(nullptr)));
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    const auto instructionCount = instructions.size();
    BOOST_CHECK_THROW(type->emitLoadFunctionVariable(compiler, 0, ExpressionTarget::Stack), NomadException);
    BOOST_CHECK_THROW(type->emitLoadParameter(compiler, 0, ExpressionTarget::Stack), NomadException);
    BOOST_TEST(instructions.size() == instructionCount);
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

BOOST_AUTO_TEST_CASE(stored_property_address_opcode_borrows_nested_storage_from_a_retained_stack_owner) {
    Runtime runtime;
    const auto innerId = runtime.registerStruct("AddressInner");
    const auto payloadId = runtime.registerStruct("AddressPayload");
    const auto ownerId = runtime.registerClass("AddressOwner", ClassMode::Closed);
    const auto* inner = runtime.getTypeByName("AddressInner");
    const auto* payload = runtime.getTypeByName("AddressPayload");
    const auto* ownerType = runtime.getTypeByName("AddressOwner")->asClassType();
    BOOST_REQUIRE(runtime.registerProperty(innerId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(payloadId, "before", runtime.getIntegerType()).isValid());
    const auto nestedId = runtime.registerProperty(payloadId, "inner", inner);
    BOOST_REQUIRE(nestedId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(payloadId, "after", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "prefix", runtime.getIntegerType()).isValid());
    const auto propertyId = runtime.registerProperty(ownerId, "payload", payload);
    BOOST_REQUIRE(propertyId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "suffix", runtime.getIntegerType()).isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    const auto firstOffset = ownerType->getFieldOffset(propertyId) +
        payload->asCompositeType()->getFieldOffset(nestedId);
    const auto lastOffset = firstOffset + inner->getSize() - 1;
    BOOST_TEST(firstOffset == 2U);
    BOOST_TEST(lastOffset == 3U);
    const auto opcodeId = runtime.getInstructionId(OpCodes::op_stored_property_address_push);
    BOOST_REQUIRE(opcodeId.isValid());
    const auto& operands = runtime.getInstructionOperands(opcodeId);
    BOOST_REQUIRE(operands.size() == 3U);
    for (const auto operand : operands) {
        BOOST_CHECK(operand == Operand::Index);
    }
    const auto instruction = runtime.getInstructionFn(opcodeId);
    BOOST_REQUIRE(instruction != nullptr);
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    {
        ScriptObject object(ownerType);
        object.getStorageValue(0).setIntegerValue(101);
        object.getStorageValue(1).setIntegerValue(202);
        object.getStorageValue(2).setIntegerValue(17);
        object.getStorageValue(3).setStringValue("original");
        object.getStorageValue(4).setIntegerValue(404);
        object.getStorageValue(5).setIntegerValue(505);
        RuntimeValue owner;
        owner.setObjectValue(&object);
        auto compiler = runtime.createCompiler();
        for (const auto ownerOffset : {NomadIndex{0}, NomadIndex{1}}) {
            RuntimeValue destination;
            const auto name = "addressOwner" + std::to_string(ownerOffset);
            const auto setupId = runtime.registerNativeFunction(name + "Setup", [&, ownerOffset](VirtualMachine* active) {
                active->pushObjectValue(owner);
                if (ownerOffset != 0) {
                    active->pushStorageAddress(&destination);
                }
                active->setIntegerResult(71);
                active->setIntermediate(RuntimeValue{NomadInteger{83}});
            }, {}, runtime.getVoidType(), "");
            const auto inspectId = runtime.registerNativeFunction(name + "Inspect", [&, ownerOffset](VirtualMachine* active) {
                BOOST_TEST(active->getIntegerResult() == 71);
                BOOST_TEST(active->getIntermediate().getIntegerValue() == 83);
                BOOST_TEST(object.getReferenceCount() == 2U);
                BOOST_TEST(active->peekStack(ownerOffset + 1).getObjectValue() == &object);
                if (ownerOffset != 0) {
                    BOOST_TEST(active->peekStack(1).getStorageAddress() == &destination);
                }
                auto* address = active->peekStack(0).getStorageAddress();
                BOOST_REQUIRE(address == &object.getStorageValue(firstOffset));
                address[0].setIntegerValue(42);
                address[1].freeObjectValue();
                address[1].setStringValue("updated");
                BOOST_TEST(object.getStorageValue(2).getIntegerValue() == 42);
                BOOST_TEST(NomadString(object.getStorageValue(3).getStringValue()) == "updated");
                BOOST_TEST(object.getStorageValue(0).getIntegerValue() == 101);
                BOOST_TEST(object.getStorageValue(1).getIntegerValue() == 202);
                BOOST_TEST(object.getStorageValue(4).getIntegerValue() == 404);
                BOOST_TEST(object.getStorageValue(5).getIntegerValue() == 505);
                active->setIntegerResult(address[0].getIntegerValue());
            }, {}, runtime.getIntegerType(), "");
            BOOST_REQUIRE(setupId.isValid());
            BOOST_REQUIRE(inspectId.isValid());
            const auto callerId = runtime.registerScriptFunction(name, "address.nomad", "");
            auto* caller = runtime.getScriptFunction(callerId);
            BOOST_REQUIRE(caller != nullptr);
            caller->setReturnType(runtime.getIntegerType());
            caller->setFunctionStart(runtime.getInstructions().size());
            compiler->addFunctionEntry(caller);
            compiler->addOpCode(OpCodes::op_call_native_function);
            compiler->addId(setupId.id);
            compiler->addOpCode(OpCodes::op_stored_property_address_push);
            compiler->addIndex(ownerOffset);
            compiler->addIndex(firstOffset);
            compiler->addIndex(lastOffset);
            compiler->addOpCode(OpCodes::op_call_native_function);
            compiler->addId(inspectId.id);
            ownerType->emitFreeStackValue(*compiler, ownerOffset + 1);
            compiler->addOpCode(ownerOffset == 0 ? OpCodes::op_pop_2 : OpCodes::op_pop_3);
            compiler->addFunctionReturn(caller);
            caller->setFunctionEnd(runtime.getInstructions().size());
            for (auto index = 0; index < 2; ++index) {
                RuntimeValue result;
                runtime.executeFunction(callerId, {}, result);
                BOOST_TEST(result.getIntegerValue() == 42);
                BOOST_TEST(object.getReferenceCount() == 1U);
            }
            VirtualMachine machine(&runtime);
            BOOST_TEST(machine.getStoredPropertyAddress(owner, 5, 5) == &object.getStorageValue(5));
            BOOST_TEST(machine.getStoredPropertyAddress(owner, 0, 5) == &object.getStorageValue(0));
            std::ostringstream dump;
            dumpInstructions(runtime, *caller, dump);
            BOOST_TEST(dump.str().find("op_stored_property_address_push") != NomadString::npos);
        }
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(stored_property_address_opcode_rejects_missing_ranges_and_non_storage_receivers) {
    Runtime runtime;
    const auto pairId = runtime.registerStruct("AddressPair");
    const auto emptyId = runtime.registerStruct("AddressEmpty");
    const auto ownerId = runtime.registerClass("GrowingAddressOwner", ClassMode::Opened);
    const auto emptyOwnerId = runtime.registerClass("EmptyAddressOwner", ClassMode::Closed);
    const auto* pair = runtime.getTypeByName("AddressPair");
    const auto* empty = runtime.getTypeByName("AddressEmpty");
    const auto* ownerType = runtime.getTypeByName("GrowingAddressOwner")->asClassType();
    const auto* emptyOwnerType = runtime.getTypeByName("EmptyAddressOwner")->asClassType();
    BOOST_REQUIRE(runtime.registerProperty(pairId, "first", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "last", runtime.getIntegerType()).isValid());
    runtime.finalizeCompositeFieldLayout(pairId);
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "prefix", runtime.getIntegerType()).isValid());
    ScriptObject oldObject(ownerType);
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "pair", pair).isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    ScriptObject newObject(ownerType);
    BOOST_REQUIRE(runtime.registerProperty(emptyOwnerId, "empty", empty).isValid());
    runtime.finalizeCompositeFieldLayout(emptyOwnerId);
    ScriptObject emptyObject(emptyOwnerType);
    BOOST_TEST(oldObject.getStorageSize() == 1U);
    BOOST_TEST(newObject.getStorageSize() == 3U);
    BOOST_TEST(emptyObject.getFieldCount() == 1U);
    BOOST_TEST(emptyObject.getStorageSize() == 0U);
    RuntimeValue oldOwner;
    oldOwner.setObjectValue(&oldObject);
    RuntimeValue newOwner;
    newOwner.setObjectValue(&newObject);
    RuntimeValue emptyOwner;
    emptyOwner.setObjectValue(&emptyObject);
    const auto instruction = runtime.getInstructionFn(
        runtime.getInstructionId(OpCodes::op_stored_property_address_push));
    BOOST_REQUIRE(instruction != nullptr);
    auto compiler = runtime.createCompiler();
    auto caseCount = 0;
    const auto expectFault = [&](
        const RuntimeValue& owner,
        const NomadIndex firstOffset,
        const NomadIndex lastOffset,
        const NomadString& diagnostic
    ) {
        const auto name = "addressFault" + std::to_string(caseCount++);
        const auto setupId = runtime.registerNativeFunction(name + "Setup", [&](VirtualMachine* active) {
            // External owners outlive the faulting run; this stack alias acquires no extra reference.
            active->pushValue(owner);
            active->setIntegerResult(71);
            active->setIntermediate(RuntimeValue{NomadInteger{83}});
        }, {}, runtime.getVoidType(), "");
        BOOST_REQUIRE(setupId.isValid());
        const auto callerId = runtime.registerScriptFunction(name, "addressFault.nomad", "");
        auto* caller = runtime.getScriptFunction(callerId);
        BOOST_REQUIRE(caller != nullptr);
        caller->setReturnType(runtime.getVoidType());
        caller->setFunctionStart(runtime.getInstructions().size());
        compiler->addFunctionEntry(caller);
        compiler->addOpCode(OpCodes::op_call_native_function);
        compiler->addId(setupId.id);
        compiler->addOpCode(OpCodes::op_stored_property_address_push);
        compiler->addIndex(0);
        compiler->addIndex(firstOffset);
        compiler->addIndex(lastOffset);
        compiler->addOpCode(OpCodes::op_pop_2);
        compiler->addFunctionReturn(caller);
        caller->setFunctionEnd(runtime.getInstructions().size());
        VirtualMachine machine(&runtime);
        const auto instructionIndex = machine.getInstructionIndex();
        BOOST_CHECK_EXCEPTION(machine.run(caller, {}), VirtualMachineException, [&](const auto& error) {
            return NomadString(error.what()).find(diagnostic) != NomadString::npos;
        });
        BOOST_TEST(machine.getInstructionIndex() == instructionIndex);
        BOOST_TEST(machine.getIntegerResult() == 71);
        BOOST_TEST(machine.getIntermediate().getIntegerValue() == 83);
    };
    expectFault(oldOwner, 1, 2, "out of bounds");
    expectFault(oldOwner, 0, 2, "out of bounds");
    expectFault(oldOwner, 0, toNomadIndex(NOMAD_ID_MAX) + 1, "out of bounds");
    expectFault(emptyOwner, 0, 0, "out of bounds");
    expectFault(RuntimeValue{}, 0, 0, "no matching field storage");
    RuntimeValue nonStorageOwner;
    nonStorageOwner.setStringValue("not script storage");
    expectFault(nonStorageOwner, 0, 0, "no matching field storage");
    nonStorageOwner.freeObjectValue();
    VirtualMachine machine(&runtime);
    BOOST_TEST(machine.getStoredPropertyAddress(oldOwner, 0, 0) == &oldObject.getStorageValue(0));
    BOOST_TEST(machine.getStoredPropertyAddress(newOwner, 1, 2) == &newObject.getStorageValue(1));
}

BOOST_AUTO_TEST_CASE(local_cleanup_emits_only_owned_fields_at_full_width_nested_offsets) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("EmptyCleanup");
    const auto innerId = runtime.registerStruct("InnerCleanup");
    const auto outerId = runtime.registerStruct("OuterCleanup");
    const auto scalarId = runtime.registerStruct("ScalarCleanup");
    const auto* empty = runtime.getTypeByName("EmptyCleanup")->asStructType();
    const auto* inner = runtime.getTypeByName("InnerCleanup")->asStructType();
    const auto* outer = runtime.getTypeByName("OuterCleanup")->asStructType();
    const auto* scalar = runtime.getTypeByName("ScalarCleanup")->asStructType();
    const auto ownerId = runtime.registerClass("CleanupOwner", ClassMode::Closed);
    BOOST_REQUIRE(ownerId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "name", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "before", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "inner", inner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "empty", empty).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "owner", runtime.getTypeByName("CleanupOwner")).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "label", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(scalarId, "number", runtime.getIntegerType()).isValid());
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    const auto initialSize = instructions.size();
    BOOST_CHECK_THROW(outer->emitInitFunctionVariable(compiler, 0), NomadException);
    BOOST_CHECK_THROW(outer->emitFreeFunctionVariable(compiler, 0), NomadException);
    BOOST_TEST(instructions.size() == initialSize);
    runtime.finalizeCompositeFieldLayout(outerId);
    runtime.finalizeCompositeFieldLayout(scalarId);
    const auto offset = toNomadIndex(NOMAD_ID_MAX) + 1;
    empty->emitInitFunctionVariable(compiler, offset);
    scalar->emitInitFunctionVariable(compiler, offset);
    empty->emitFreeFunctionVariable(compiler, offset);
    scalar->emitFreeFunctionVariable(compiler, offset);
    BOOST_TEST(instructions.size() == initialSize);
    outer->emitFreeFunctionVariable(compiler, offset);
    BOOST_REQUIRE(instructions.size() == initialSize + 6);
    for (auto index = NomadIndex{0}; index < 3; ++index) {
        const auto position = initialSize + index * 2;
        BOOST_TEST(runtime.getInstructionId(instructions[position].fn).id ==
            runtime.getInstructionId(OpCodes::op_object_free_variable).id);
        BOOST_TEST(instructions[position + 1].value.getIndexValue() == offset + index + 2);
    }
    instructions.clear();
    outer->emitInitFunctionVariable(compiler, offset);
    BOOST_REQUIRE(instructions.size() == 6U);
    for (auto index = NomadIndex{0}; index < 2; ++index) {
        const auto position = index * 3;
        BOOST_TEST(runtime.getInstructionId(instructions[position].fn).id ==
            runtime.getInstructionId(OpCodes::op_string_init_r).id);
        BOOST_TEST(runtime.getInstructionId(instructions[position + 1].fn).id ==
            runtime.getInstructionId(OpCodes::op_function_variable_set).id);
        BOOST_TEST(instructions[position + 2].value.getIndexValue() == offset + (index == 0 ? 2 : 4));
    }
}

BOOST_AUTO_TEST_CASE(function_return_releases_nested_struct_locals_and_preserves_scalar_neighbors) {
    const auto before = RuntimeValue::getLiveStringCount();
    Runtime runtime;
    RuntimeValue emptyDefault;
    runtime.getStringType()->initValue(&emptyDefault);
    const auto emptyReferences = emptyDefault.getObjectValue()->getReferenceCount();
    const auto innerId = runtime.registerStruct("LocalPayload");
    const auto outerId = runtime.registerStruct("LocalWrapper");
    const auto* inner = runtime.getTypeByName("LocalPayload")->asStructType();
    const auto* outer = runtime.getTypeByName("LocalWrapper")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(innerId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "name", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "payload", inner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "label", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(outerId);
    const auto id = runtime.registerScriptFunction("cleanupLocals", "cleanup.nomad", "");
    auto* function = runtime.getScriptFunction(id);
    BOOST_REQUIRE(function != nullptr);
    function->setReturnType(runtime.getIntegerType());
    const auto leading = function->registerVariable("leading", runtime.getIntegerType());
    const auto value = function->registerVariable("value", outer);
    const auto untouched = function->registerVariable("untouched", outer);
    const auto trailing = function->registerVariable("trailing", runtime.getIntegerType());
    const auto initialize = runtime.registerNativeFunction("initializeCleanupLocals",
        [&](VirtualMachine* machine) {
            BOOST_TEST(machine->getFunctionIntegerVariableValue(leading) == 0);
            BOOST_TEST(machine->getFunctionIntegerVariableValue(trailing) == 0);
            machine->setIntegerVariableValue(leading, 17);
            machine->setIntegerVariableValue(trailing, 29);
            const auto storage = machine->getLocalStorage(function->getVariableOffset(value), outer->getSize());
            const auto untouchedStorage = machine->getLocalStorage(
                function->getVariableOffset(untouched), outer->getSize());
            BOOST_TEST(emptyDefault.getObjectValue()->getReferenceCount() == emptyReferences + 4);
            for (const auto values : {storage, untouchedStorage}) {
                BOOST_TEST(values[0].getIntegerValue() == 0);
                for (auto index = NomadIndex{1}; index < 3; ++index) {
                    BOOST_REQUIRE(values[index].getObjectValue() != nullptr);
                    BOOST_TEST(NomadString(values[index].getStringValue()) == "");
                }
            }
            storage[0].setIntegerValue(31);
            storage[1].freeObjectValue();
            storage[2].freeObjectValue();
            storage[1].setStringValue("nested");
            storage[2].setStringValue("label");
            BOOST_TEST(RuntimeValue::getLiveStringCount() == before + 2);
        }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(initialize.isValid());
    auto compiler = runtime.createCompiler();
    function->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(function);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(initialize.id);
    compiler->addLoadFunctionVariable(function->getVariableOffset(trailing), runtime.getIntegerType(),
        ExpressionTarget::Result);
    compiler->addFunctionReturn(function);
    function->setFunctionEnd(runtime.getInstructions().size());
    for (auto index = 0; index < 2; ++index) {
        RuntimeValue result;
        runtime.executeFunction(id, {}, result);
        BOOST_TEST(result.getIntegerValue() == 29);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == before);
        BOOST_TEST(emptyDefault.getObjectValue()->getReferenceCount() == emptyReferences);
    }
    runtime.getStringType()->freeValue(&emptyDefault);
}

BOOST_AUTO_TEST_CASE(local_copy_emission_preserves_full_width_offsets_and_empty_self_copy) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("EmptyCopy");
    const auto innerId = runtime.registerStruct("InnerCopy");
    const auto outerId = runtime.registerStruct("OuterCopy");
    const auto* empty = runtime.getTypeByName("EmptyCopy")->asStructType();
    const auto* inner = runtime.getTypeByName("InnerCopy")->asStructType();
    const auto* outer = runtime.getTypeByName("OuterCopy")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(innerId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "empty", empty).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "inner", inner).isValid());
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    const auto initialSize = instructions.size();
    BOOST_CHECK_THROW(outer->emitCopyFunctionVariable(compiler, 0, 3), NomadException);
    BOOST_CHECK_THROW(outer->emitAssignFunctionVariable(compiler, 0, 3, 6), NomadException);
    BOOST_CHECK_THROW(outer->emitCopyFunctionVariable(compiler, 0, 0), NomadException);
    BOOST_TEST(instructions.size() == initialSize);
    runtime.finalizeCompositeFieldLayout(outerId);
    const auto source = toNomadIndex(NOMAD_ID_MAX) + 1;
    const auto destination = source + 3;
    const auto scratch = source + 6;
    empty->emitCopyFunctionVariable(compiler, 0, std::numeric_limits<NomadIndex>::max());
    empty->emitAssignFunctionVariable(compiler, 0, 1, std::numeric_limits<NomadIndex>::max());
    outer->emitCopyFunctionVariable(compiler, source, source);
    outer->emitAssignFunctionVariable(compiler, source, source, scratch);
    BOOST_TEST(instructions.size() == initialSize);
    instructions.clear();
    outer->emitCopyFunctionVariable(compiler, source, destination);
    BOOST_REQUIRE(instructions.size() == 10U);
    const std::array<NomadIndex, 5> positions{0, 2, 4, 6, 8};
    const std::array<NomadString, 5> names{
        OpCodes::op_function_variable_get_r, OpCodes::op_function_variable_set,
        OpCodes::op_function_variable_object_get_r, OpCodes::op_object_free_variable,
        OpCodes::op_function_variable_set
    };
    const std::array<NomadIndex, 5> operands{source, destination, source + 1, destination + 1, destination + 1};
    for (auto index = NomadIndex{0}; index < positions.size(); ++index) {
        BOOST_TEST(runtime.getInstructionId(instructions[positions[index]].fn).id ==
            runtime.getInstructionId(names[index]).id);
        BOOST_TEST(instructions[positions[index] + 1].value.getIndexValue() == operands[index]);
    }
    instructions.clear();
    outer->emitAssignFunctionVariable(compiler, source, destination, scratch);
    BOOST_REQUIRE(instructions.size() == 22U);
    const std::array<NomadIndex, 10> assignmentOperands{
        source, scratch, source + 1, scratch + 1, scratch + 1,
        scratch, destination, scratch + 1, destination + 1, destination + 1
    };
    for (auto index = NomadIndex{0}; index < assignmentOperands.size(); ++index) {
        BOOST_TEST(instructions[index * 2 + 1].value.getIndexValue() == assignmentOperands[index]);
    }
    BOOST_TEST(runtime.getInstructionId(instructions[20].fn).id ==
        runtime.getInstructionId(OpCodes::op_object_free_variable).id);
    BOOST_TEST(instructions[21].value.getIndexValue() == scratch + 1);
}

BOOST_AUTO_TEST_CASE(local_copy_and_assignment_preserve_nested_values_and_reference_ownership) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("CopyMarker");
    const auto innerId = runtime.registerStruct("CopyPayload");
    const auto outerId = runtime.registerStruct("CopyWrapper");
    const auto ownerId = runtime.registerClass("CopyOwner", ClassMode::Closed);
    const auto* empty = runtime.getTypeByName("CopyMarker")->asStructType();
    const auto* inner = runtime.getTypeByName("CopyPayload")->asStructType();
    const auto* outer = runtime.getTypeByName("CopyWrapper")->asStructType();
    const auto* owner = runtime.getTypeByName("CopyOwner")->asClassType();
    BOOST_REQUIRE(ownerId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "marker", empty).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "inner", inner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "owner", owner).isValid());
    runtime.finalizeCompositeFieldLayout(outerId);
    const auto id = runtime.registerScriptFunction("copyLocalStorage", "copy.nomad", "");
    auto* function = runtime.getScriptFunction(id);
    BOOST_REQUIRE(function != nullptr);
    function->setReturnType(runtime.getIntegerType());
    const auto sourceId = function->registerVariable("source", outer);
    const auto destinationId = function->registerVariable("destination", outer);
    const auto scratchId = function->registerVariable("scratch", outer);
    const auto source = function->getVariableOffset(sourceId);
    const auto destination = function->getVariableOffset(destinationId);
    const auto scratch = function->getVariableOffset(scratchId);
    const auto initialize = runtime.registerNativeFunction("initializeCopyStorage",
        [&](VirtualMachine* machine) {
            for (const auto offset : {source, destination, scratch}) {
                const auto values = machine->getLocalStorage(offset, outer->getSize());
                values[0].setIntegerValue(offset == source ? 17 : 99);
                values[1].freeObjectValue();
                values[1].setStringValue(offset == source ? "source" : "old");
                values[2].setObjectValue(new ScriptObject(owner));
            }
        }, {}, runtime.getVoidType(), "");
    const auto inspectCopy = runtime.registerNativeFunction("inspectCopyStorage",
        [&](VirtualMachine* machine) {
            const auto sourceValues = machine->getLocalStorage(source, outer->getSize());
            const auto destinationValues = machine->getLocalStorage(destination, outer->getSize());
            BOOST_TEST(destinationValues[0].getIntegerValue() == 17);
            BOOST_TEST(destinationValues[1].getObjectValue() == sourceValues[1].getObjectValue());
            BOOST_TEST(destinationValues[2].getObjectValue() == sourceValues[2].getObjectValue());
            BOOST_TEST(sourceValues[1].getObjectValue()->getReferenceCount() == 2U);
            BOOST_TEST(sourceValues[2].getObjectValue()->getReferenceCount() == 2U);
            destinationValues[0].setIntegerValue(23);
            BOOST_TEST(sourceValues[0].getIntegerValue() == 17);
        }, {}, runtime.getVoidType(), "");
    const auto inspectAssignment = runtime.registerNativeFunction("inspectAssignedStorage",
        [&](VirtualMachine* machine) {
            const auto sourceValues = machine->getLocalStorage(source, outer->getSize());
            const auto destinationValues = machine->getLocalStorage(destination, outer->getSize());
            const auto scratchValues = machine->getLocalStorage(scratch, outer->getSize());
            BOOST_TEST(destinationValues[0].getIntegerValue() == 17);
            BOOST_TEST(sourceValues[1].getObjectValue()->getReferenceCount() == 2U);
            BOOST_TEST(sourceValues[2].getObjectValue()->getReferenceCount() == 2U);
            BOOST_TEST(scratchValues[1].getObjectValue() == nullptr);
            BOOST_TEST(scratchValues[2].getObjectValue() == nullptr);
        }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(initialize.isValid() && inspectCopy.isValid() && inspectAssignment.isValid());
    auto compiler = runtime.createCompiler();
    function->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(function);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(initialize.id);
    outer->emitCopyFunctionVariable(*compiler, source, destination);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(inspectCopy.id);
    for (auto index = 0; index < 2; ++index) {
        outer->emitAssignFunctionVariable(*compiler, source, destination, scratch);
        outer->emitAssignFunctionVariable(*compiler, source, source, scratch);
        compiler->addOpCode(OpCodes::op_call_native_function);
        compiler->addId(inspectAssignment.id);
    }
    compiler->addLoadFunctionVariable(destination, runtime.getIntegerType(), ExpressionTarget::Result);
    compiler->addFunctionReturn(function);
    function->setFunctionEnd(runtime.getInstructions().size());
    RuntimeValue emptyDefault;
    runtime.getStringType()->initValue(&emptyDefault);
    const auto emptyReferences = emptyDefault.getObjectValue()->getReferenceCount();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 2; ++index) {
        RuntimeValue result;
        runtime.executeFunction(id, {}, result);
        BOOST_TEST(result.getIntegerValue() == 17);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
        BOOST_TEST(emptyDefault.getObjectValue()->getReferenceCount() == emptyReferences);
    }
    runtime.getStringType()->freeValue(&emptyDefault);
}

BOOST_AUTO_TEST_CASE(local_assignment_stages_overlapping_reference_ranges_in_both_directions) {
    Runtime runtime;
    const auto pairId = runtime.registerStruct("OverlapPair");
    const auto backingId = runtime.registerStruct("OverlapBacking");
    const auto* pair = runtime.getTypeByName("OverlapPair")->asStructType();
    const auto* backing = runtime.getTypeByName("OverlapBacking")->asStructType();
    for (const auto* name : {"first", "second"}) {
        BOOST_REQUIRE(runtime.registerProperty(pairId, name, runtime.getStringType()).isValid());
    }
    for (const auto* name : {"first", "second", "third"}) {
        BOOST_REQUIRE(runtime.registerProperty(backingId, name, runtime.getStringType()).isValid());
    }
    runtime.finalizeCompositeFieldLayout(pairId);
    runtime.finalizeCompositeFieldLayout(backingId);
    const auto id = runtime.registerScriptFunction("overlapLocalStorage", "overlap.nomad", "");
    auto* function = runtime.getScriptFunction(id);
    BOOST_REQUIRE(function != nullptr);
    function->setReturnType(runtime.getVoidType());
    const auto valuesId = function->registerVariable("values", backing);
    const auto scratchId = function->registerVariable("scratch", pair);
    const auto values = function->getVariableOffset(valuesId);
    const auto scratch = function->getVariableOffset(scratchId);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto initialize = runtime.registerNativeFunction("initializeOverlapStorage",
        [&](VirtualMachine* machine) {
            const auto storage = machine->getLocalStorage(values, 3);
            const std::array<NomadString, 3> texts{"first", "second", "third"};
            for (auto index = NomadIndex{0}; index < 3; ++index) {
                storage[index].freeObjectValue();
                storage[index].setStringValue(texts[index]);
            }
        }, {}, runtime.getVoidType(), "");
    const auto inspectForward = runtime.registerNativeFunction("inspectForwardOverlap",
        [&](VirtualMachine* machine) {
            const auto storage = machine->getLocalStorage(values, 3);
            BOOST_TEST(NomadString(storage[0].getStringValue()) == "first");
            BOOST_TEST(NomadString(storage[1].getStringValue()) == "first");
            BOOST_TEST(NomadString(storage[2].getStringValue()) == "second");
            BOOST_TEST(storage[0].getObjectValue()->getReferenceCount() == 2U);
            BOOST_TEST(storage[2].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore + 2);
        }, {}, runtime.getVoidType(), "");
    const auto inspectReverse = runtime.registerNativeFunction("inspectReverseOverlap",
        [&](VirtualMachine* machine) {
            const auto storage = machine->getLocalStorage(values, 3);
            BOOST_TEST(NomadString(storage[0].getStringValue()) == "first");
            BOOST_TEST(NomadString(storage[1].getStringValue()) == "second");
            BOOST_TEST(NomadString(storage[2].getStringValue()) == "second");
            BOOST_TEST(storage[0].getObjectValue()->getReferenceCount() == 1U);
            BOOST_TEST(storage[1].getObjectValue()->getReferenceCount() == 2U);
            for (const auto& value : machine->getLocalStorage(scratch, 2)) {
                BOOST_TEST(value.getObjectValue() == nullptr);
            }
        }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(initialize.isValid() && inspectForward.isValid() && inspectReverse.isValid());
    auto compiler = runtime.createCompiler();
    function->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(function);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(initialize.id);
    pair->emitAssignFunctionVariable(*compiler, values, values + 1, scratch);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(inspectForward.id);
    pair->emitAssignFunctionVariable(*compiler, values + 1, values, scratch);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(inspectReverse.id);
    compiler->addFunctionReturn(function);
    function->setFunctionEnd(runtime.getInstructions().size());
    for (auto index = 0; index < 2; ++index) {
        RuntimeValue result;
        runtime.executeFunction(id, {}, result);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    }
}

BOOST_AUTO_TEST_CASE(expressions_compile_into_local_storage_without_aggregate_registers) {
    Runtime runtime;
    const auto innerId = runtime.registerStruct("ExpressionPayload");
    const auto outerId = runtime.registerStruct("ExpressionWrapper");
    const auto* inner = runtime.getTypeByName("ExpressionPayload")->asStructType();
    const auto* outer = runtime.getTypeByName("ExpressionWrapper")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(innerId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "payload", inner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "label", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(outerId);
    const auto id = runtime.registerScriptFunction("expressionDestinations", "expressions.nomad", "");
    auto* function = runtime.getScriptFunction(id);
    BOOST_REQUIRE(function != nullptr);
    function->setReturnType(runtime.getIntegerType());
    const auto source = function->registerVariable("source", outer);
    const auto destination = function->registerVariable("destination", outer);
    const auto number = function->registerVariable("number", runtime.getIntegerType());
    const auto text = function->registerVariable("text", runtime.getStringType());
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    IdentifierExpression sourceExpression(nullptr, 1, 1, "source");
    IntegerLiteral numberExpression(nullptr, 1, 1, 47);
    StringLiteral textExpression(nullptr, 1, 1, "literal");
    BOOST_REQUIRE(sourceExpression.resolve(&context, function));
    BOOST_REQUIRE(numberExpression.resolve(&context, function));
    BOOST_REQUIRE(textExpression.resolve(&context, function));
    BOOST_TEST(!sourceExpression.hasValue());
    const auto initialize = runtime.registerNativeFunction("initializeExpressionLocals",
        [&](VirtualMachine* machine) {
            const auto values = machine->getLocalStorage(function->getVariableOffset(source), outer->getSize());
            values[0].setIntegerValue(17);
            values[1].freeObjectValue();
            values[2].freeObjectValue();
            values[1].setStringValue("nested");
            values[2].setStringValue("label");
        }, {}, runtime.getVoidType(), "");
    const auto inspect = runtime.registerNativeFunction("inspectExpressionDestinations",
        [&](VirtualMachine* machine) {
            const auto sourceValues = machine->getLocalStorage(function->getVariableOffset(source), outer->getSize());
            const auto destinationValues = machine->getLocalStorage(
                function->getVariableOffset(destination), outer->getSize());
            BOOST_TEST(destinationValues[0].getIntegerValue() == 17);
            BOOST_TEST(destinationValues[1].getObjectValue() == sourceValues[1].getObjectValue());
            BOOST_TEST(destinationValues[2].getObjectValue() == sourceValues[2].getObjectValue());
            BOOST_TEST(sourceValues[1].getObjectValue()->getReferenceCount() == 2U);
            BOOST_TEST(machine->getFunctionIntegerVariableValue(number) == 47);
            BOOST_TEST(NomadString(machine->getFunctionStringVariableValue(text)->getString()) == "literal");
            destinationValues[0].setIntegerValue(23);
            BOOST_TEST(sourceValues[0].getIntegerValue() == 17);
        }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(initialize.isValid() && inspect.isValid());
    function->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(function);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(initialize.id);
    sourceExpression.compileIntoLocal(compiler.get(), function, destination);
    sourceExpression.compileIntoLocal(compiler.get(), function, source);
    numberExpression.compileIntoLocal(compiler.get(), function, number);
    textExpression.compileIntoLocal(compiler.get(), function, text);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(inspect.id);
    compiler->addLoadFunctionVariable(function->getVariableOffset(number), runtime.getIntegerType(),
        ExpressionTarget::Result);
    compiler->addFunctionReturn(function);
    function->setFunctionEnd(runtime.getInstructions().size());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    for (auto index = 0; index < 2; ++index) {
        RuntimeValue result;
        runtime.executeFunction(id, {}, result);
        BOOST_TEST(result.getIntegerValue() == 47);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    }
}

BOOST_AUTO_TEST_CASE(local_expression_destinations_handle_empty_structs_and_reject_invalid_targets) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("EmptyExpression");
    const auto* empty = runtime.getTypeByName("EmptyExpression")->asStructType();
    runtime.finalizeCompositeFieldLayout(emptyId);
    ScriptFunction function(CallableId{}, "destinationChecks", "checks.nomad", "");
    const auto source = function.registerVariable("source", empty);
    const auto destination = function.registerVariable("destination", empty);
    const auto wrongType = function.registerVariable("number", runtime.getIntegerType());
    function.addParameter("parameter", empty, "");
    BOOST_REQUIRE(runtime.registerNativeFunction("getEmptyExpression",
        [](VirtualMachine*) {}, {}, empty, "").isValid());
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    CompilerContext context(&compiler);
    IdentifierExpression expression(nullptr, 1, 1, "source");
    IdentifierExpression parameter(nullptr, 1, 1, "parameter");
    FunctionCallExpression call(nullptr, 1, 1, "getEmptyExpression");
    IntegerLiteral unresolved(nullptr, 1, 1, 3);
    BOOST_REQUIRE(expression.resolve(&context, &function));
    BOOST_REQUIRE(parameter.resolve(&context, &function));
    BOOST_REQUIRE(call.resolve(&context, &function));
    const auto initialSize = instructions.size();
    expression.compileIntoLocal(&compiler, &function, destination);
    expression.compileIntoLocal(&compiler, &function, source);
    BOOST_TEST(instructions.size() == initialSize);
    parameter.compileIntoLocal(&compiler, &function, destination);
    BOOST_CHECK_THROW(expression.compileIntoLocal(&compiler, &function, wrongType), NomadBug);
    BOOST_CHECK_THROW(unresolved.compileIntoLocal(&compiler, &function, wrongType), NomadBug);
    BOOST_TEST(instructions.size() == initialSize);
    BOOST_CHECK_THROW(call.compile(&compiler, &function, ExpressionTarget::Result), CompilerException);
    BOOST_TEST(instructions.size() == initialSize);
    call.compileIntoLocal(&compiler, &function, destination);
    BOOST_TEST(instructions.size() > initialSize);
}

BOOST_AUTO_TEST_CASE(struct_local_assignments_allocate_staging_before_entry_and_release_it_after_use) {
    Runtime runtime;
    const auto innerId = runtime.registerStruct("AssignmentPayload");
    const auto outerId = runtime.registerStruct("AssignmentWrapper");
    const auto* inner = runtime.getTypeByName("AssignmentPayload")->asStructType();
    const auto* outer = runtime.getTypeByName("AssignmentWrapper")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(innerId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "payload", inner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "label", runtime.getStringType()).isValid());
    const auto id = runtime.registerScriptFunction("stagedAssignment", "assignment.nomad", "");
    auto* function = runtime.getScriptFunction(id);
    BOOST_REQUIRE(function != nullptr);
    function->setReturnType(runtime.getIntegerType());
    const auto source = function->registerVariable("source", outer);
    const auto destination = function->registerVariable("destination", outer);
    const auto number = function->registerVariable("number", runtime.getIntegerType());
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    AssignmentStatement assignment(1, 1, "destination",
        std::make_unique<IdentifierExpression>(nullptr, 1, 1, "source"));
    AssignmentStatement selfAssignment(2, 1, "source",
        std::make_unique<IdentifierExpression>(nullptr, 2, 1, "source"));
    AssignmentStatement scalarAssignment(3, 1, "number",
        std::make_unique<IntegerLiteral>(nullptr, 3, 1, 47));
    BOOST_REQUIRE(assignment.resolve(&context, function));
    BOOST_REQUIRE(selfAssignment.resolve(&context, function));
    BOOST_REQUIRE(scalarAssignment.resolve(&context, function));
    BOOST_TEST(function->getVariableCount() == 5U);
    BOOST_REQUIRE(assignment.resolve(&context, function));
    BOOST_REQUIRE(selfAssignment.resolve(&context, function));
    BOOST_TEST(function->getVariableCount() == 5U);
    const auto firstStaging = VariableId{3};
    const auto secondStaging = VariableId{4};
    BOOST_TEST(function->getVariableType(firstStaging) == outer);
    BOOST_TEST(function->getVariableType(secondStaging) == outer);
    BOOST_TEST(function->getVisibleVariableName(firstStaging).empty());
    BOOST_TEST(function->getVisibleVariableName(secondStaging).empty());
    BOOST_TEST(!outer->isFieldLayoutFinalized());
    runtime.finalizeCompositeFieldLayout(outerId);
    BOOST_TEST(function->getVariableStorageSize() == 13U);
    BOOST_TEST(function->getVariableOffset(firstStaging) == 7U);
    BOOST_TEST(function->getVariableOffset(secondStaging) == 10U);
    const auto initialize = runtime.registerNativeFunction("initializeAssignmentStorage",
        [&](VirtualMachine* machine) {
            for (const auto variable : {source, destination}) {
                const auto values = machine->getLocalStorage(function->getVariableOffset(variable), outer->getSize());
                values[0].setIntegerValue(variable == source ? 17 : 99);
                for (auto index = NomadIndex{1}; index < 3; ++index) {
                    values[index].freeObjectValue();
                    values[index].setStringValue(variable == source ? "source" : "old");
                }
            }
        }, {}, runtime.getVoidType(), "");
    const auto inspect = runtime.registerNativeFunction("inspectStagedAssignment",
        [&](VirtualMachine* machine) {
            const auto sourceValues = machine->getLocalStorage(function->getVariableOffset(source), outer->getSize());
            const auto destinationValues = machine->getLocalStorage(
                function->getVariableOffset(destination), outer->getSize());
            BOOST_TEST(sourceValues[0].getIntegerValue() == 17);
            BOOST_TEST(destinationValues[0].getIntegerValue() == 17);
            BOOST_TEST(machine->getFunctionIntegerVariableValue(number) == 47);
            for (auto index = NomadIndex{1}; index < 3; ++index) {
                BOOST_TEST(sourceValues[index].getObjectValue() == destinationValues[index].getObjectValue());
                BOOST_TEST(sourceValues[index].getObjectValue()->getReferenceCount() == 2U);
            }
            for (const auto staging : {firstStaging, secondStaging}) {
                const auto values = machine->getLocalStorage(function->getVariableOffset(staging), outer->getSize());
                BOOST_TEST(values[1].getObjectValue() == nullptr);
                BOOST_TEST(values[2].getObjectValue() == nullptr);
            }
            destinationValues[0].setIntegerValue(23);
            BOOST_TEST(sourceValues[0].getIntegerValue() == 17);
        }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(initialize.isValid() && inspect.isValid());
    function->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(function);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(initialize.id);
    for (auto index = 0; index < 2; ++index) {
        assignment.compile(compiler.get(), function);
        selfAssignment.compile(compiler.get(), function);
        scalarAssignment.compile(compiler.get(), function);
        compiler->addOpCode(OpCodes::op_call_native_function);
        compiler->addId(inspect.id);
    }
    compiler->addLoadFunctionVariable(function->getVariableOffset(number), runtime.getIntegerType(),
        ExpressionTarget::Result);
    compiler->addFunctionReturn(function);
    function->setFunctionEnd(runtime.getInstructions().size());
    RuntimeValue emptyDefault;
    runtime.getStringType()->initValue(&emptyDefault);
    const auto emptyReferences = emptyDefault.getObjectValue()->getReferenceCount();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    for (auto index = 0; index < 2; ++index) {
        RuntimeValue result;
        runtime.executeFunction(id, {}, result);
        BOOST_TEST(result.getIntegerValue() == 47);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(emptyDefault.getObjectValue()->getReferenceCount() == emptyReferences);
    }
    runtime.getStringType()->freeValue(&emptyDefault);
}

BOOST_AUTO_TEST_CASE(empty_struct_assignment_staging_occupies_no_physical_slots) {
    Runtime runtime;
    const auto id = runtime.registerStruct("EmptyAssignment");
    const auto* empty = runtime.getTypeByName("EmptyAssignment")->asStructType();
    runtime.finalizeCompositeFieldLayout(id);
    ScriptFunction function(CallableId{}, "emptyAssignment", "empty.nomad", "");
    function.registerVariable("source", empty);
    function.registerVariable("destination", empty);
    function.addParameter("parameter", empty, "");
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    CompilerContext context(&compiler);
    AssignmentStatement assignment(1, 1, "destination",
        std::make_unique<IdentifierExpression>(nullptr, 1, 1, "source"));
    BOOST_REQUIRE(assignment.resolve(&context, &function));
    BOOST_REQUIRE(assignment.resolve(&context, &function));
    BOOST_TEST(function.getVariableCount() == 3U);
    BOOST_TEST(function.getVariableStorageSize() == 0U);
    const auto initialSize = instructions.size();
    assignment.compile(&compiler, &function);
    BOOST_TEST(instructions.size() == initialSize);
    AssignmentStatement parameterAssignment(2, 1, "parameter",
        std::make_unique<IdentifierExpression>(nullptr, 2, 1, "source"));
    AssignmentStatement parameterSelfAssignment(3, 1, "parameter",
        std::make_unique<IdentifierExpression>(nullptr, 3, 1, "parameter"));
    BOOST_REQUIRE(parameterAssignment.resolve(&context, &function));
    BOOST_REQUIRE(parameterSelfAssignment.resolve(&context, &function));
    BOOST_REQUIRE(parameterAssignment.resolve(&context, &function));
    BOOST_TEST(function.getVariableCount() == 5U);
    BOOST_TEST(function.getVariableStorageSize() == 0U);
    parameterAssignment.compile(&compiler, &function);
    parameterSelfAssignment.compile(&compiler, &function);
    BOOST_TEST(instructions.size() == initialSize);
}

BOOST_AUTO_TEST_CASE(scalar_parameter_bytecode_uses_offsets_after_wide_parameters) {
    Runtime runtime;
    const auto paddingId = runtime.registerStruct("ParameterPadding");
    const auto* padding = runtime.getTypeByName("ParameterPadding")->asStructType();
    for (const auto* name : {"x", "y", "z"}) {
        BOOST_REQUIRE(runtime.registerProperty(paddingId, name, runtime.getIntegerType()).isValid());
    }
    runtime.finalizeCompositeFieldLayout(paddingId);
    const auto innerId = runtime.registerScriptFunction("offsetParameters", "parameters.nomad", "");
    auto* inner = runtime.getScriptFunction(innerId);
    BOOST_REQUIRE(inner != nullptr);
    inner->setReturnType(runtime.getIntegerType());
    inner->addParameter("padding", padding, "");
    const auto textId = inner->addParameter("text", runtime.getStringType(), "");
    const auto numberId = inner->addParameter("number", runtime.getIntegerType(), "");
    BOOST_TEST(inner->getParameterOffset(textId) == 3U);
    BOOST_TEST(inner->getParameterOffset(numberId) == 4U);
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    AssignmentStatement textAssignment(1, 1, "text",
        std::make_unique<StringLiteral>(nullptr, 1, 1, "replacement"));
    AssignmentStatement numberAssignment(2, 1, "number",
        std::make_unique<IntegerLiteral>(nullptr, 2, 1, 47));
    IdentifierExpression textExpression(nullptr, 3, 1, "text");
    IdentifierExpression numberExpression(nullptr, 3, 1, "number");
    BOOST_REQUIRE(textAssignment.resolve(&context, inner));
    BOOST_REQUIRE(numberAssignment.resolve(&context, inner));
    BOOST_REQUIRE(textExpression.resolve(&context, inner));
    BOOST_REQUIRE(numberExpression.resolve(&context, inner));
    const auto inspect = runtime.registerNativeFunction("inspectOffsetParameters",
        [&](VirtualMachine* machine) {
            const auto point = machine->getEnclosingParameterStorage(0, 3);
            BOOST_TEST(point[0].getIntegerValue() == 1);
            BOOST_TEST(point[2].getIntegerValue() == 3);
            BOOST_TEST(NomadString(machine->getEnclosingParameterStorage(3, 1)[0].getStringValue()) == "replacement");
            BOOST_TEST(machine->getEnclosingParameterStorage(4, 1)[0].getIntegerValue() == 47);
        }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(inspect.isValid());
    inner->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(inner);
    textAssignment.compile(compiler.get(), inner);
    numberAssignment.compile(compiler.get(), inner);
    for (const auto target : {ExpressionTarget::Result, ExpressionTarget::Intermediate, ExpressionTarget::Stack}) {
        textExpression.compile(compiler.get(), inner, target);
        if (target == ExpressionTarget::Result) {
            runtime.getStringType()->emitFreeResult(*compiler);
        } else if (target == ExpressionTarget::Intermediate) {
            runtime.getStringType()->emitFreeIntermediate(*compiler);
        } else {
            runtime.getStringType()->emitFreeStackValue(*compiler, 0);
            compiler->addOpCode(OpCodes::op_pop_n);
            compiler->addIndex(1);
        }
        numberExpression.compile(compiler.get(), inner, target);
        if (target == ExpressionTarget::Stack) {
            compiler->addOpCode(OpCodes::op_pop_n);
            compiler->addIndex(1);
        }
    }
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(inspect.id);
    numberExpression.compile(compiler.get(), inner, ExpressionTarget::Result);
    compiler->addFunctionReturn(inner);
    inner->setFunctionEnd(runtime.getInstructions().size());
    const auto outerId = runtime.registerScriptFunction("callOffsetParameters", "caller.nomad", "");
    auto* outer = runtime.getScriptFunction(outerId);
    BOOST_REQUIRE(outer != nullptr);
    outer->setReturnType(runtime.getIntegerType());
    outer->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(outer);
    compiler->addLoadIntegerValue(99, ExpressionTarget::Stack);
    compiler->addLoadStringValue("original", ExpressionTarget::Stack);
    for (const auto value : {1, 2, 3}) {
        compiler->addLoadIntegerValue(value, ExpressionTarget::Stack);
    }
    compiler->addOpCode(OpCodes::op_call_script_function);
    compiler->addIndex(inner->getFunctionStart());
    runtime.getStringType()->emitFreeStackValue(*compiler, 3);
    compiler->addOpCode(OpCodes::op_pop_n);
    compiler->addIndex(5);
    compiler->addFunctionReturn(outer);
    outer->setFunctionEnd(runtime.getInstructions().size());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    for (auto index = 0; index < 2; ++index) {
        RuntimeValue result;
        runtime.executeFunction(outerId, {}, result);
        BOOST_TEST(result.getIntegerValue() == 47);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    }
    std::ostringstream dump;
    dumpInstructions(runtime, *inner, dump);
    BOOST_TEST(dump.str().find("op_parameter_load_r") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(parameter_copy_and_cleanup_use_reversed_nested_field_offsets) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("ParameterCopyEmpty");
    const auto innerId = runtime.registerStruct("ParameterCopyInner");
    const auto outerId = runtime.registerStruct("ParameterCopyOuter");
    const auto* empty = runtime.getTypeByName("ParameterCopyEmpty")->asStructType();
    const auto* inner = runtime.getTypeByName("ParameterCopyInner")->asStructType();
    const auto* outer = runtime.getTypeByName("ParameterCopyOuter")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(innerId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "before", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "inner", inner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "empty", empty).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "label", runtime.getStringType()).isValid());
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    const auto initialSize = instructions.size();
    BOOST_CHECK_THROW(outer->emitCopyParameterToFunctionVariable(compiler, 0, 0), NomadException);
    BOOST_CHECK_THROW(outer->emitCopyFunctionVariableToParameter(compiler, 0, 0), NomadException);
    BOOST_CHECK_THROW(outer->emitFreeParameter(compiler, 0), NomadException);
    BOOST_CHECK_THROW(outer->emitPushFunctionVariable(compiler, 0), NomadException);
    BOOST_CHECK_THROW(outer->emitFreeStackValue(compiler, 0), NomadException);
    BOOST_TEST(instructions.size() == initialSize);
    runtime.finalizeCompositeFieldLayout(outerId);
    const auto offset = toNomadIndex(NOMAD_ID_MAX) + 1;
    empty->emitCopyParameterToFunctionVariable(compiler, offset, offset);
    empty->emitCopyFunctionVariableToParameter(compiler, offset, offset);
    empty->emitFreeParameter(compiler, offset);
    empty->emitPushFunctionVariable(compiler, offset);
    empty->emitFreeStackValue(compiler, offset);
    BOOST_TEST(instructions.size() == initialSize);
    instructions.clear();
    outer->emitCopyParameterToFunctionVariable(compiler, offset, offset + 10);
    BOOST_REQUIRE(instructions.size() == 20U);
    const std::array<NomadIndex, 4> positions{0, 4, 8, 14};
    const std::array<NomadIndex, 4> parameterOffsets{3, 2, 1, 0};
    const std::array<NomadIndex, 4> destinationPositions{3, 7, 13, 19};
    for (auto index = NomadIndex{0}; index < positions.size(); ++index) {
        BOOST_TEST(instructions[positions[index] + 1].value.getIndexValue() == offset + parameterOffsets[index]);
        BOOST_TEST(instructions[destinationPositions[index]].value.getIndexValue() == offset + 10 + index);
    }
    instructions.clear();
    outer->emitFreeParameter(compiler, offset);
    BOOST_REQUIRE(instructions.size() == 4U);
    BOOST_TEST(instructions[1].value.getIndexValue() == offset + 1);
    BOOST_TEST(instructions[3].value.getIndexValue() == offset);
    for (const auto position : {0, 2}) {
        BOOST_TEST(runtime.getInstructionId(instructions[position].fn).id ==
            runtime.getInstructionId(OpCodes::op_object_free_parameter).id);
    }
    instructions.clear();
    outer->emitCopyFunctionVariableToParameter(compiler, offset + 10, offset);
    BOOST_REQUIRE(instructions.size() == 20U);
    for (auto index = NomadIndex{0}; index < positions.size(); ++index) {
        BOOST_TEST(instructions[positions[index] + 1].value.getIndexValue() == offset + 10 + index);
        BOOST_TEST(instructions[destinationPositions[index]].value.getIndexValue() == offset + parameterOffsets[index]);
        BOOST_TEST(runtime.getInstructionId(instructions[destinationPositions[index] - 1].fn).id ==
            runtime.getInstructionId(OpCodes::op_parameter_set).id);
    }
    instructions.clear();
    outer->emitPushFunctionVariable(compiler, offset);
    BOOST_REQUIRE(instructions.size() == 8U);
    for (auto index = NomadIndex{0}; index < 4; ++index) {
        BOOST_TEST(instructions[index * 2 + 1].value.getIndexValue() == offset + index);
        BOOST_TEST(runtime.getInstructionId(instructions[index * 2].fn).id ==
            runtime.getInstructionId(index < 2 ? OpCodes::op_function_variable_push :
                OpCodes::op_function_variable_object_push).id);
    }
    instructions.clear();
    BOOST_TEST(outer->emitLoadFunctionVariable(compiler, offset, ExpressionTarget::Stack) == 1U);
    BOOST_REQUIRE(instructions.size() == 8U);
    for (auto index = NomadIndex{0}; index < 4; ++index) {
        BOOST_TEST(instructions[index * 2 + 1].value.getIndexValue() == offset + index);
    }
    instructions.clear();
    BOOST_TEST(outer->emitLoadParameter(compiler, offset, ExpressionTarget::Stack) == 1U);
    BOOST_REQUIRE(instructions.size() == 8U);
    for (auto index = NomadIndex{0}; index < 4; ++index) {
        BOOST_TEST(instructions[index * 2 + 1].value.getIndexValue() == offset + 3 - index);
        BOOST_TEST(runtime.getInstructionId(instructions[index * 2].fn).id ==
            runtime.getInstructionId(index < 2 ? OpCodes::op_parameter_push :
                OpCodes::op_object_parameter_push).id);
    }
    instructions.clear();
    outer->emitFreeStackValue(compiler, offset);
    BOOST_REQUIRE(instructions.size() == 4U);
    BOOST_TEST(instructions[1].value.getIndexValue() == offset + 1);
    BOOST_TEST(instructions[3].value.getIndexValue() == offset);
}

BOOST_AUTO_TEST_CASE(struct_parameter_assignment_to_local_copies_without_mutating_caller_storage) {
    Runtime runtime;
    const auto innerTypeId = runtime.registerStruct("BorrowedPayload");
    const auto outerTypeId = runtime.registerStruct("BorrowedWrapper");
    const auto ownerId = runtime.registerClass("ParameterOwner", ClassMode::Closed);
    const auto* innerType = runtime.getTypeByName("BorrowedPayload")->asStructType();
    const auto* outerType = runtime.getTypeByName("BorrowedWrapper")->asStructType();
    const auto* owner = runtime.getTypeByName("ParameterOwner")->asClassType();
    BOOST_REQUIRE(ownerId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerTypeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerTypeId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerTypeId, "owner", owner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerTypeId, "inner", innerType).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerTypeId, "label", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(outerTypeId);
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const auto calleeId = runtime.registerScriptFunction("copyParameter", "parameter.nomad", "");
    auto* callee = runtime.getScriptFunction(calleeId);
    BOOST_REQUIRE(callee != nullptr);
    callee->setReturnType(runtime.getIntegerType());
    callee->addParameter("leading", runtime.getIntegerType(), "");
    callee->addParameter("value", outerType, "");
    callee->addParameter("trailing", runtime.getIntegerType(), "");
    const auto destination = callee->registerVariable("destination", outerType);
    AssignmentStatement assignment(1, 1, "destination",
        std::make_unique<IdentifierExpression>(nullptr, 1, 1, "value"));
    AssignmentStatement replaceParameter(2, 1, "value",
        std::make_unique<IdentifierExpression>(nullptr, 2, 1, "destination"));
    AssignmentStatement selfParameter(3, 1, "value",
        std::make_unique<IdentifierExpression>(nullptr, 3, 1, "value"));
    BOOST_REQUIRE(assignment.resolve(&context, callee));
    BOOST_REQUIRE(replaceParameter.resolve(&context, callee));
    BOOST_REQUIRE(selfParameter.resolve(&context, callee));
    const auto variableCount = callee->getVariableCount();
    BOOST_REQUIRE(replaceParameter.resolve(&context, callee));
    BOOST_REQUIRE(selfParameter.resolve(&context, callee));
    BOOST_TEST(callee->getVariableCount() == variableCount);
    const auto inspect = runtime.registerNativeFunction("inspectParameterCopy",
        [&](VirtualMachine* machine) {
            const auto parameter = machine->getEnclosingParameterStorage(1, 4);
            const auto local = machine->getLocalStorage(callee->getVariableOffset(destination), 4);
            BOOST_TEST(machine->getEnclosingParameterStorage(0, 1)[0].getIntegerValue() == 11);
            BOOST_TEST(machine->getEnclosingParameterStorage(5, 1)[0].getIntegerValue() == 29);
            BOOST_TEST(local[0].getIntegerValue() == 17);
            for (auto index = NomadIndex{1}; index < 4; ++index) {
                BOOST_TEST(local[index].getObjectValue() == parameter[index].getObjectValue());
                BOOST_TEST(local[index].getObjectValue()->getReferenceCount() == 3U);
            }
            local[0].setIntegerValue(47);
            BOOST_TEST(parameter[0].getIntegerValue() == 17);
            local[1].freeObjectValue();
            local[1].setStringValue("replacement");
            local[2].freeObjectValue();
            local[2].setObjectValue(new ScriptObject(owner));
            local[3].freeObjectValue();
            local[3].setStringValue("replaced label");
        }, {}, runtime.getVoidType(), "");
    const auto inspectReplacement = runtime.registerNativeFunction("inspectReplacedParameter",
        [&](VirtualMachine* machine) {
            const auto parameter = machine->getEnclosingParameterStorage(1, 4);
            const auto local = machine->getLocalStorage(callee->getVariableOffset(destination), 4);
            BOOST_TEST(parameter[0].getIntegerValue() == 47);
            BOOST_TEST(NomadString(parameter[1].getStringValue()) == "replacement");
            BOOST_TEST(NomadString(parameter[3].getStringValue()) == "replaced label");
            for (auto index = NomadIndex{1}; index < 4; ++index) {
                BOOST_TEST(parameter[index].getObjectValue() == local[index].getObjectValue());
                BOOST_TEST(parameter[index].getObjectValue()->getReferenceCount() == 2U);
            }
            for (auto index = NomadIndex{1}; index < callee->getVariableCount(); ++index) {
                const auto staging = VariableId{toNomadId(index)};
                BOOST_TEST(callee->getVisibleVariableName(staging).empty());
                const auto values = machine->getLocalStorage(callee->getVariableOffset(staging), 4);
                for (auto field = NomadIndex{1}; field < 4; ++field) {
                    BOOST_TEST(values[field].getObjectValue() == nullptr);
                }
            }
            BOOST_TEST(machine->getEnclosingParameterStorage(0, 1)[0].getIntegerValue() == 11);
            BOOST_TEST(machine->getEnclosingParameterStorage(5, 1)[0].getIntegerValue() == 29);
        }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(inspect.isValid());
    BOOST_REQUIRE(inspectReplacement.isValid());
    const auto inspectForwarded = runtime.registerNativeFunction("inspectForwardedStruct",
        [&](VirtualMachine* machine) {
            const auto values = machine->getParameterStorage(0, 4);
            BOOST_TEST(values[0].getIntegerValue() == 17);
            BOOST_TEST(NomadString(values[1].getStringValue()) == "nested");
            for (auto index = NomadIndex{1}; index < 4; ++index) {
                BOOST_TEST(values[index].getObjectValue()->getReferenceCount() == 4U);
            }
            values[0].setIntegerValue(99);
            BOOST_TEST(machine->getEnclosingParameterStorage(1, 4)[0].getIntegerValue() == 17);
        }, {defParameter("value", outerType, "")}, runtime.getVoidType(), "");
    BOOST_REQUIRE(inspectForwarded.isValid());
    ArgumentList forwarded;
    forwarded.addExpressionArgument(1, 1, outerType,
        std::make_unique<IdentifierExpression>(nullptr, 1, 1, "value"));
    BOOST_REQUIRE(forwarded.resolve(&context, callee));
    BOOST_REQUIRE(forwarded.resolve(&context, callee));
    BOOST_TEST(callee->getVariableCount() == variableCount);
    BOOST_TEST(forwarded.getArgumentCount() == 4U);
    callee->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(callee);
    assignment.compile(compiler.get(), callee);
    compiler->addNativeFunctionCall(inspectForwarded, callee, &forwarded);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(inspect.id);
    for (auto index = 0; index < 2; ++index) {
        replaceParameter.compile(compiler.get(), callee);
        selfParameter.compile(compiler.get(), callee);
        compiler->addOpCode(OpCodes::op_call_native_function);
        compiler->addId(inspectReplacement.id);
    }
    outerType->emitFreeParameter(*compiler, 1);
    compiler->addLoadFunctionVariable(callee->getVariableOffset(destination), runtime.getIntegerType(),
        ExpressionTarget::Result);
    compiler->addFunctionReturn(callee);
    callee->setFunctionEnd(runtime.getInstructions().size());
    const auto callerId = runtime.registerScriptFunction("callerParameterStorage", "caller.nomad", "");
    auto* caller = runtime.getScriptFunction(callerId);
    BOOST_REQUIRE(caller != nullptr);
    caller->setReturnType(runtime.getIntegerType());
    const auto source = caller->registerVariable("source", outerType);
    const auto returnedNumber = caller->registerVariable("returnedNumber", runtime.getIntegerType());
    ArgumentList arguments;
    arguments.addExpressionArgument(1, 1, runtime.getIntegerType(),
        std::make_unique<IntegerLiteral>(nullptr, 1, 1, 11));
    arguments.addExpressionArgument(1, 1, outerType,
        std::make_unique<IdentifierExpression>(nullptr, 1, 1, "source"));
    arguments.addExpressionArgument(1, 1, runtime.getIntegerType(),
        std::make_unique<IntegerLiteral>(nullptr, 1, 1, 29));
    BOOST_REQUIRE(arguments.resolve(&context, caller));
    BOOST_REQUIRE(arguments.resolve(&context, caller));
    BOOST_TEST(caller->getVariableCount() == 2U);
    BOOST_TEST(caller->getVariableStorageSize() == 5U);
    BOOST_TEST(arguments.getArgumentCount() == 6U);
    const auto initialize = runtime.registerNativeFunction("initializeCallerParameter",
        [&](VirtualMachine* machine) {
            const auto storage = machine->getLocalStorage(caller->getVariableOffset(source), 4);
            storage[0].setIntegerValue(17);
            storage[1].freeObjectValue();
            storage[3].freeObjectValue();
            storage[1].setStringValue("nested");
            storage[2].setObjectValue(new ScriptObject(owner));
            storage[3].setStringValue("label");
        }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(initialize.isValid());
    const auto inspectCaller = runtime.registerNativeFunction("inspectOriginalCallerValue",
        [&](VirtualMachine* machine) {
            const auto storage = machine->getLocalStorage(caller->getVariableOffset(source), 4);
            BOOST_TEST(storage[0].getIntegerValue() == 17);
            BOOST_TEST(NomadString(storage[1].getStringValue()) == "nested");
            BOOST_TEST(NomadString(storage[3].getStringValue()) == "label");
            for (auto index = NomadIndex{1}; index < 4; ++index) {
                BOOST_TEST(storage[index].getObjectValue()->getReferenceCount() == 1U);
            }
        }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(inspectCaller.isValid());
    caller->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(caller);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(initialize.id);
    arguments.compile(compiler.get(), caller);
    compiler->addOpCode(OpCodes::op_call_script_function);
    compiler->addIndex(callee->getFunctionStart());
    compiler->addFreeArguments(&arguments);
    runtime.getIntegerType()->emitStoreFunctionVariable(*compiler, caller->getVariableOffset(returnedNumber));
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(inspectCaller.id);
    compiler->addLoadFunctionVariable(caller->getVariableOffset(returnedNumber), runtime.getIntegerType(),
        ExpressionTarget::Result);
    compiler->addFunctionReturn(caller);
    caller->setFunctionEnd(runtime.getInstructions().size());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 2; ++index) {
        RuntimeValue result;
        runtime.executeFunction(callerId, {}, result);
        BOOST_TEST(result.getIntegerValue() == 47);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(mixed_struct_and_empty_arguments_keep_right_to_left_evaluation_and_cleanup) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("EmptyArgument");
    const auto pairId = runtime.registerStruct("PairArgument");
    const auto* empty = runtime.getTypeByName("EmptyArgument")->asStructType();
    const auto* pair = runtime.getTypeByName("PairArgument")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(pairId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(emptyId);
    runtime.finalizeCompositeFieldLayout(pairId);
    const auto id = runtime.registerScriptFunction("mixedArguments", "arguments.nomad", "");
    auto* function = runtime.getScriptFunction(id);
    BOOST_REQUIRE(function != nullptr);
    function->setReturnType(runtime.getIntegerType());
    const auto source = function->registerVariable("source", pair);
    function->registerVariable("empty", empty);
    std::vector<NomadInteger> order;
    for (const auto number : {1, 2}) {
        BOOST_REQUIRE(runtime.registerNativeFunction("argument" + std::to_string(number),
            [&, number](VirtualMachine* machine) {
                order.push_back(number);
                machine->getResult().setIntegerValue(number);
            }, {}, runtime.getIntegerType(), "").isValid());
    }
    const auto inspect = runtime.registerNativeFunction("inspectMixedArguments",
        [&](VirtualMachine* machine) {
            BOOST_REQUIRE(order.size() == 2U);
            BOOST_TEST(order[0] == 2);
            BOOST_TEST(order[1] == 1);
            BOOST_TEST(machine->getParameterStorage(0, 1)[0].getIntegerValue() == 1);
            BOOST_TEST(machine->getParameterStorage(5, 1)[0].getIntegerValue() == 2);
            const auto first = machine->getParameterStorage(1, 2);
            const auto second = machine->getParameterStorage(3, 2);
            BOOST_TEST(first[0].getIntegerValue() == 17);
            BOOST_TEST(second[0].getIntegerValue() == 17);
            BOOST_TEST(first[1].getObjectValue() == second[1].getObjectValue());
            BOOST_TEST(first[1].getObjectValue()->getReferenceCount() == 3U);
            first[0].setIntegerValue(23);
            BOOST_TEST(second[0].getIntegerValue() == 17);
            machine->getResult().setIntegerValue(47);
        }, {defParameter("left", runtime.getIntegerType(), ""), defParameter("first", pair, ""),
            defParameter("empty", empty, ""), defParameter("second", pair, ""),
            defParameter("right", runtime.getIntegerType(), "")}, runtime.getIntegerType(), "");
    BOOST_REQUIRE(inspect.isValid());
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    ArgumentList arguments;
    arguments.addExpressionArgument(1, 1, runtime.getIntegerType(),
        std::make_unique<FunctionCallExpression>(nullptr, 1, 1, "argument1"));
    arguments.addExpressionArgument(1, 1, pair,
        std::make_unique<IdentifierExpression>(nullptr, 1, 1, "source"));
    arguments.addExpressionArgument(1, 1, empty,
        std::make_unique<IdentifierExpression>(nullptr, 1, 1, "empty"));
    arguments.addExpressionArgument(1, 1, pair,
        std::make_unique<IdentifierExpression>(nullptr, 1, 1, "source"));
    arguments.addExpressionArgument(1, 1, runtime.getIntegerType(),
        std::make_unique<FunctionCallExpression>(nullptr, 1, 1, "argument2"));
    BOOST_REQUIRE(arguments.resolve(&context, function));
    BOOST_REQUIRE(arguments.resolve(&context, function));
    BOOST_TEST(function->getVariableCount() == 2U);
    BOOST_TEST(function->getVariableStorageSize() == 2U);
    BOOST_TEST(arguments.getStackCount() == 6U);
    const auto initialize = runtime.registerNativeFunction("initializeMixedArgument",
        [&](VirtualMachine* machine) {
            const auto values = machine->getLocalStorage(function->getVariableOffset(source), 2);
            values[0].setIntegerValue(17);
            values[1].freeObjectValue();
            values[1].setStringValue("source");
        }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(initialize.isValid());
    function->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(function);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(initialize.id);
    compiler->addNativeFunctionCall(inspect, function, &arguments);
    compiler->addFunctionReturn(function);
    function->setFunctionEnd(runtime.getInstructions().size());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    for (auto index = 0; index < 2; ++index) {
        order.clear();
        RuntimeValue result;
        runtime.executeFunction(id, {}, result);
        BOOST_TEST(result.getIntegerValue() == 47);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    }
}

BOOST_AUTO_TEST_CASE(borrowed_storage_emission_resolves_nested_offsets_and_ownership_opcodes) {
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("StorageEmpty");
    const auto innerId = runtime.registerStruct("StorageInner");
    const auto outerId = runtime.registerStruct("StorageOuter");
    const auto* empty = runtime.getTypeByName("StorageEmpty")->asStructType();
    const auto* inner = runtime.getTypeByName("StorageInner")->asStructType();
    const auto* outer = runtime.getTypeByName("StorageOuter")->asStructType();
    BOOST_REQUIRE(runtime.registerProperty(innerId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "before", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "inner", inner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "empty", empty).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "label", runtime.getStringType()).isValid());
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    const auto initialSize = instructions.size();
    BOOST_CHECK_THROW(outer->emitCopyFunctionVariableToStorageParameter(compiler, 0, 0, 0), NomadException);
    BOOST_CHECK_THROW(outer->emitCopyParameterToStorageParameter(compiler, 0, 0, 0), NomadException);
    BOOST_CHECK_THROW(outer->emitCopyStorageParameterToFunctionVariable(compiler, 0, 0, 0), NomadException);
    BOOST_CHECK_THROW(outer->emitFreeStorageParameter(compiler, 0, 0), NomadException);
    BOOST_CHECK_THROW(outer->emitLoadStorageParameter(compiler, 0, 0, ExpressionTarget::Stack), NomadException);
    BOOST_TEST(instructions.size() == initialSize);
    runtime.finalizeCompositeFieldLayout(outerId);
    const auto wide = toNomadIndex(NOMAD_ID_MAX) + 1;
    empty->emitCopyFunctionVariableToStorageParameter(compiler, wide, wide, wide);
    empty->emitCopyParameterToStorageParameter(compiler, wide, wide, wide);
    empty->emitCopyStorageParameterToFunctionVariable(compiler, wide, wide, wide);
    empty->emitFreeStorageParameter(compiler, wide, wide);
    BOOST_TEST(empty->emitLoadStorageParameter(compiler, wide, wide, ExpressionTarget::Stack) == NOMAD_INVALID_INDEX);
    runtime.getVoidType()->emitCopyFunctionVariableToStorageParameter(compiler, wide, wide, wide);
    BOOST_TEST(instructions.size() == initialSize);
    for (const auto target : {ExpressionTarget::Result, ExpressionTarget::Intermediate}) {
        BOOST_CHECK_THROW(outer->emitLoadStorageParameter(compiler, wide, wide, target), NomadException);
    }
    BOOST_TEST(instructions.size() == initialSize);
    const std::array<NomadIndex, 4> starts{0, 5, 10, 18};
    const std::array<NomadIndex, 4> stores{2, 7, 15, 23};
    for (const auto parameterSource : {false, true}) {
        instructions.clear();
        if (parameterSource) {
            outer->emitCopyParameterToStorageParameter(compiler, wide, wide + 10, wide + 20);
        } else {
            outer->emitCopyFunctionVariableToStorageParameter(compiler, wide, wide + 10, wide + 20);
        }
        BOOST_REQUIRE(instructions.size() == 26U);
        for (auto index = NomadIndex{0}; index < 4; ++index) {
            BOOST_TEST(instructions[starts[index] + 1].value.getIndexValue() ==
                wide + (parameterSource ? 3 - index : index));
            BOOST_TEST(instructions[stores[index] + 1].value.getIndexValue() == wide + 10);
            BOOST_TEST(instructions[stores[index] + 2].value.getIndexValue() == wide + 20 + index);
            BOOST_TEST(runtime.getInstructionId(instructions[stores[index]].fn).id ==
                runtime.getInstructionId(OpCodes::op_storage_parameter_set).id);
        }
        for (const auto position : {NomadIndex{12}, NomadIndex{20}}) {
            BOOST_TEST(runtime.getInstructionId(instructions[position].fn).id ==
                runtime.getInstructionId(OpCodes::op_object_free_storage_parameter).id);
        }
    }
    instructions.clear();
    outer->emitCopyStorageParameterToFunctionVariable(compiler, wide + 10, wide + 20, wide);
    BOOST_REQUIRE(instructions.size() == 24U);
    const std::array<NomadIndex, 4> loads{0, 5, 10, 17};
    const std::array<NomadIndex, 4> destinations{4, 9, 16, 23};
    for (auto index = NomadIndex{0}; index < 4; ++index) {
        BOOST_TEST(instructions[loads[index] + 1].value.getIndexValue() == wide + 10);
        BOOST_TEST(instructions[loads[index] + 2].value.getIndexValue() == wide + 20 + index);
        BOOST_TEST(instructions[destinations[index]].value.getIndexValue() == wide + index);
        BOOST_TEST(runtime.getInstructionId(instructions[loads[index]].fn).id ==
            runtime.getInstructionId(index < 2 ? OpCodes::op_storage_parameter_load_r :
                OpCodes::op_object_storage_parameter_load_r).id);
    }
    instructions.clear();
    BOOST_TEST(outer->emitLoadStorageParameter(compiler, wide + 10, wide + 20, ExpressionTarget::Stack) == 1U);
    BOOST_REQUIRE(instructions.size() == 12U);
    for (auto index = NomadIndex{0}; index < 4; ++index) {
        BOOST_TEST(instructions[index * 3 + 1].value.getIndexValue() == wide + 10);
        BOOST_TEST(instructions[index * 3 + 2].value.getIndexValue() == wide + 20 + index);
        BOOST_TEST(runtime.getInstructionId(instructions[index * 3].fn).id ==
            runtime.getInstructionId(index < 2 ? OpCodes::op_storage_parameter_push :
                OpCodes::op_object_storage_parameter_push).id);
    }
    instructions.clear();
    outer->emitFreeStorageParameter(compiler, wide + 10, wide + 20);
    BOOST_REQUIRE(instructions.size() == 6U);
    BOOST_TEST(instructions[2].value.getIndexValue() == wide + 22);
    BOOST_TEST(instructions[5].value.getIndexValue() == wide + 23);
    for (const auto target : {ExpressionTarget::Result, ExpressionTarget::Intermediate, ExpressionTarget::Stack}) {
        instructions.clear();
        runtime.getStringType()->emitLoadStorageParameter(compiler, wide, wide + 1, target);
        BOOST_REQUIRE(instructions.size() == 3U);
        BOOST_TEST(instructions[1].value.getIndexValue() == wide);
        BOOST_TEST(instructions[2].value.getIndexValue() == wide + 1);
    }
}

BOOST_AUTO_TEST_CASE(borrowed_destination_copies_preserve_nested_values_and_release_replaced_ownership) {
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("BorrowCopyEmpty");
    const auto innerId = runtime.registerStruct("BorrowCopyInner");
    const auto outerId = runtime.registerStruct("BorrowCopyOuter");
    const auto ownerId = runtime.registerClass("BorrowCopyOwner", ClassMode::Closed);
    const auto* empty = runtime.getTypeByName("BorrowCopyEmpty");
    const auto* inner = runtime.getTypeByName("BorrowCopyInner");
    const auto* outer = runtime.getTypeByName("BorrowCopyOuter");
    const auto* owner = runtime.getTypeByName("BorrowCopyOwner")->asClassType();
    BOOST_REQUIRE(runtime.registerProperty(innerId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "owner", owner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "before", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "inner", inner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "empty", empty).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "label", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(outerId);
    runtime.finalizeCompositeFieldLayout(ownerId);
    BOOST_REQUIRE(outer->getSize() == 5U);
    std::array<RuntimeValue, 5> parameters;
    parameters[0].setIntegerValue(11);
    parameters[1].setIntegerValue(12);
    parameters[2].setStringValue("parameter");
    parameters[3].setObjectValue(new ScriptObject(owner));
    parameters[4].setStringValue("parameter label");
    std::array<RuntimeValue, 7> destination;
    destination.front().setIntegerValue(101);
    destination.back().setIntegerValue(103);
    outer->initValue(destination.data() + 1);
    VirtualMachine machine(&runtime, 64);
    machine.pushStorageAddress(destination.data());
    for (auto index = NomadIndex{0}; index < parameters.size(); ++index) {
        if (index < 2) {
            machine.pushValue(parameters[index]);
        } else {
            machine.pushObjectValue(parameters[index]);
        }
    }
    machine.callScriptFunction(0);
    machine.reserveLocals(13);
    const auto locals = machine.getLocalStorage(0, 13);
    locals[0].setIntegerValue(107);
    locals[6].setIntegerValue(109);
    locals[12].setIntegerValue(113);
    auto compiler = runtime.createCompiler();
    const auto execute = [&](const auto& emit) {
        const auto start = runtime.getInstructions().size();
        emit();
        const auto end = runtime.getInstructions().size();
        machine.jump(start);
        while (machine.getInstructionIndex() < end) {
            const auto address = machine.getInstructionIndex();
            machine.jump(address + 1);
            runtime.getInstructions()[address].fn(&machine);
        }
    };
    execute([&] {
        outer->emitInitFunctionVariable(*compiler, 1);
        outer->emitInitFunctionVariable(*compiler, 7);
    });
    locals[1].setIntegerValue(31);
    locals[2].setIntegerValue(32);
    locals[3].freeObjectValue();
    locals[3].setStringValue("local");
    locals[4].setObjectValue(new ScriptObject(owner));
    locals[5].freeObjectValue();
    locals[5].setStringValue("local label");
    for (auto index = 0; index < 3; ++index) {
        execute([&] { outer->emitCopyFunctionVariableToStorageParameter(*compiler, 1, 5, 1); });
        BOOST_TEST(destination[1].getIntegerValue() == 31);
        BOOST_TEST(destination[2].getIntegerValue() == 32);
        BOOST_TEST(destination[4].getObjectValue() == locals[4].getObjectValue());
        BOOST_TEST(locals[4].getObjectValue()->getReferenceCount() == 2U);
        execute([&] { outer->emitCopyParameterToStorageParameter(*compiler, 0, 5, 1); });
        BOOST_TEST(locals[4].getObjectValue()->getReferenceCount() == 1U);
        BOOST_TEST(destination[1].getIntegerValue() == 11);
        BOOST_TEST(destination[2].getIntegerValue() == 12);
        BOOST_TEST(destination[3].getObjectValue() == parameters[2].getObjectValue());
        BOOST_TEST(destination[4].getObjectValue() == parameters[3].getObjectValue());
        BOOST_TEST(destination[5].getObjectValue() == parameters[4].getObjectValue());
        execute([&] { outer->emitCopyStorageParameterToFunctionVariable(*compiler, 5, 1, 7); });
        BOOST_TEST(parameters[3].getObjectValue()->getReferenceCount() == 4U);
        BOOST_TEST(locals[7].getIntegerValue() == 11);
        BOOST_TEST(locals[10].getObjectValue() == parameters[3].getObjectValue());
        locals[7].setIntegerValue(41);
        BOOST_TEST(destination[1].getIntegerValue() == 11);
        BOOST_TEST(parameters[0].getIntegerValue() == 11);
    }
    execute([&] { outer->emitLoadStorageParameter(*compiler, 5, 1, ExpressionTarget::Stack); });
    BOOST_TEST(machine.peekStack(4).getIntegerValue() == 11);
    BOOST_TEST(machine.peekStack(0).getObjectValue() == parameters[4].getObjectValue());
    BOOST_TEST(parameters[3].getObjectValue()->getReferenceCount() == 5U);
    execute([&] { outer->emitFreeStackValue(*compiler, 0); });
    machine.popN(5);
    BOOST_TEST(parameters[3].getObjectValue()->getReferenceCount() == 4U);
    execute([&] { outer->emitFreeStorageParameter(*compiler, 5, 1); });
    for (const auto index : {3, 4, 5}) {
        BOOST_TEST(destination[index].getObjectValue() == nullptr);
    }
    BOOST_TEST(destination.front().getIntegerValue() == 101);
    BOOST_TEST(destination.back().getIntegerValue() == 103);
    BOOST_TEST(locals[0].getIntegerValue() == 107);
    BOOST_TEST(locals[6].getIntegerValue() == 109);
    BOOST_TEST(locals[12].getIntegerValue() == 113);
    execute([&] {
        outer->emitFreeFunctionVariable(*compiler, 1);
        outer->emitFreeFunctionVariable(*compiler, 7);
        outer->emitFreeParameter(*compiler, 0);
    });
    BOOST_TEST(parameters[3].getObjectValue()->getReferenceCount() == 1U);
    machine.returnFunction(13);
    machine.popN(6);
    outer->freeValue(parameters.data());
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
}

BOOST_AUTO_TEST_CASE(compiler_destination_calls_emit_only_required_hidden_pointers) {
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    Runtime runtime;
    const auto pointId = runtime.registerStruct("DestinationPoint");
    const auto emptyId = runtime.registerStruct("DestinationEmpty");
    const auto* point = runtime.getTypeByName("DestinationPoint");
    const auto* empty = runtime.getTypeByName("DestinationEmpty");
    BOOST_REQUIRE(runtime.registerProperty(pointId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pointId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(pointId);
    runtime.finalizeCompositeFieldLayout(emptyId);
    const auto callerId = runtime.registerScriptFunction("destinationCaller", "destination.nomad", "");
    const auto calleeId = runtime.registerScriptFunction("destinationCallee", "destination.nomad", "");
    auto* caller = runtime.getScriptFunction(callerId);
    auto* callee = runtime.getScriptFunction(calleeId);
    caller->setReturnType(runtime.getIntegerType());
    callee->setReturnType(point);
    callee->addParameter("point", point, "");
    callee->addParameter("number", runtime.getIntegerType(), "");
    const auto sourceId = caller->registerVariable("source", point);
    const auto destinationId = caller->registerVariable("destination", point);
    const auto emptyLocal = caller->registerVariable("empty", empty);
    const auto layout = callee->getCallStorageLayout();
    BOOST_REQUIRE(layout.returnOffset.has_value());
    BOOST_TEST(*layout.returnOffset == 3U);
    BOOST_TEST(layout.stackSlotCount == 4U);
    auto evaluations = 0;
    auto emptyCalls = 0;
    const auto sideEffect = runtime.registerNativeFunction("destinationArgument", [&](VirtualMachine* active) {
        ++evaluations;
        active->setIntegerResult(3);
    }, {}, runtime.getIntegerType(), "");
    const auto initialize = runtime.registerNativeFunction("initializeDestination", [&](VirtualMachine* active) {
        const auto source = active->getLocalStorage(caller->getVariableOffset(sourceId), 2);
        source[0].setIntegerValue(17);
        source[1].freeObjectValue();
        source[1].setStringValue("source");
    }, {}, runtime.getVoidType(), "");
    const auto nativeId = runtime.registerNativeFunction("nativeDestination", [&](VirtualMachine* active) {
        BOOST_TEST(active->getIntegerParameter(2) == 3);
        auto* destination = active->getParameter(3).getStorageAddress();
        BOOST_TEST(destination == active->getLocalAddress(caller->getVariableOffset(destinationId)));
        point->copyValue(active->getParameterStorage(0, 2).data(), destination);
        destination[0].setIntegerValue(77);
    }, {defParameter("point", point, ""), defParameter("number", runtime.getIntegerType(), "")}, point, "");
    const auto emptyFunction = runtime.registerNativeFunction("emptyDestination", [&](VirtualMachine*) {
        ++emptyCalls;
    }, {}, empty, "");
    const auto inspectScript = runtime.registerNativeFunction("inspectScriptDestination", [&](VirtualMachine* active) {
        BOOST_TEST(active->getLocalStorage(caller->getVariableOffset(sourceId), 2)[0].getIntegerValue() == 17);
        BOOST_TEST(active->getLocalStorage(caller->getVariableOffset(destinationId), 2)[0].getIntegerValue() == 99);
    }, {}, runtime.getVoidType(), "");
    const auto inspect = runtime.registerNativeFunction("inspectDestination", [&](VirtualMachine* active) {
        const auto source = active->getLocalStorage(caller->getVariableOffset(sourceId), 2);
        const auto destination = active->getLocalStorage(caller->getVariableOffset(destinationId), 2);
        BOOST_TEST(source[0].getIntegerValue() == 17);
        BOOST_TEST(destination[0].getIntegerValue() == 77);
        BOOST_TEST(source[1].getObjectValue() == destination[1].getObjectValue());
        BOOST_TEST(source[1].getObjectValue()->getReferenceCount() == 2U);
    }, {}, runtime.getVoidType(), "");
    BOOST_REQUIRE(sideEffect.isValid());
    BOOST_REQUIRE(initialize.isValid());
    BOOST_REQUIRE(nativeId.isValid());
    BOOST_REQUIRE(emptyFunction.isValid());
    BOOST_REQUIRE(inspect.isValid());
    BOOST_REQUIRE(inspectScript.isValid());
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    ArgumentList arguments;
    arguments.addExpressionArgument(1, 1, point,
        std::make_unique<IdentifierExpression>(nullptr, 1, 1, "source"));
    arguments.addExpressionArgument(1, 1, runtime.getIntegerType(),
        std::make_unique<FunctionCallExpression>(nullptr, 1, 1, "destinationArgument"));
    BOOST_REQUIRE(arguments.resolve(&context, caller));
    BOOST_REQUIRE(arguments.resolve(&context, caller));
    BOOST_TEST(caller->getVariableCount() == 3U);
    BOOST_TEST(caller->getVariableStorageSize() == 4U);
    ArgumentList emptyArguments;
    const auto beforeInvalid = runtime.getInstructions().size();
    BOOST_CHECK_THROW(compiler->addFunctionCallIntoLocal(calleeId, caller, &arguments, emptyLocal), NomadBug);
    BOOST_TEST(runtime.getInstructions().size() == beforeInvalid);
    callee->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(callee);
    compiler->addLoadValue(runtime.getIntegerType(), RuntimeValue(NomadInteger{99}), ExpressionTarget::Result);
    runtime.getIntegerType()->emitStoreParameter(*compiler, 1);
    point->emitCopyParameterToStorageParameter(*compiler, 0, *layout.returnOffset, 0);
    compiler->addFunctionReturn(callee);
    callee->setFunctionEnd(runtime.getInstructions().size());
    caller->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(caller);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(initialize.id);
    const auto scriptCallStart = runtime.getInstructions().size();
    compiler->addFunctionCallIntoLocal(calleeId, caller, &arguments, destinationId);
    BOOST_TEST(runtime.getInstructionId(runtime.getInstructions()[scriptCallStart + 1].fn).id ==
        runtime.getInstructionId(OpCodes::op_local_address_push).id);
    BOOST_TEST(runtime.getInstructions()[scriptCallStart + 2].value.getIndexValue() ==
        caller->getVariableOffset(destinationId));
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(inspectScript.id);
    compiler->addFunctionCallIntoLocal(nativeId, caller, &arguments, destinationId);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(inspect.id);
    const auto emptyCallStart = runtime.getInstructions().size();
    compiler->addFunctionCallIntoLocal(emptyFunction, caller, &emptyArguments, emptyLocal);
    BOOST_TEST(runtime.getInstructions().size() == emptyCallStart + 4);
    BOOST_TEST(runtime.getInstructionId(runtime.getInstructions()[emptyCallStart].fn).id ==
        runtime.getInstructionId(OpCodes::op_push_this).id);
    BOOST_TEST(runtime.getInstructionId(runtime.getInstructions()[emptyCallStart + 1].fn).id ==
        runtime.getInstructionId(OpCodes::op_call_native_function).id);
    BOOST_TEST(runtime.getInstructionId(runtime.getInstructions()[emptyCallStart + 3].fn).id ==
        runtime.getInstructionId(OpCodes::op_pop_this).id);
    compiler->addLoadFunctionVariable(caller->getVariableOffset(destinationId), runtime.getIntegerType(),
        ExpressionTarget::Result);
    compiler->addFunctionReturn(caller);
    caller->setFunctionEnd(runtime.getInstructions().size());
    compiler->linkFunctions();
    for (auto index = 0; index < 3; ++index) {
        RuntimeValue result;
        runtime.executeFunction(callerId, {}, result);
        BOOST_TEST(result.getIntegerValue() == 77);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    }
    BOOST_TEST(evaluations == 6);
    BOOST_TEST(emptyCalls == 3);
}

BOOST_AUTO_TEST_CASE(compiler_struct_method_calls_borrow_locals_parameters_and_enclosing_receivers) {
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    Runtime runtime;
    const auto typeId = runtime.registerStruct("BorrowedPoint");
    const auto* type = runtime.getTypeByName("BorrowedPoint");
    BOOST_REQUIRE(runtime.registerProperty(typeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(typeId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(typeId);
    auto* owner = runtime.getTypeManager().getMutableCompositeType(typeId);
    const auto parameters = std::vector<Parameter>{
        defParameter("snapshot", type, ""), defParameter("delta", runtime.getIntegerType(), "")};
    auto nativeCalls = 0;
    auto readCalls = 0;
    auto argumentCalls = 0;
    const auto mutateId = owner->registerMethod(runtime, std::make_unique<NativeMethod>(
        "borrowedMutate", type, [&](VirtualMachine* active) {
            ++nativeCalls;
            auto* receiver = &active->getStorageParameter(3, 0);
            auto* destination = &active->getStorageParameter(4, 0);
            auto snapshot = active->getParameterStorage(0, 2);
            BOOST_TEST(receiver != snapshot.data());
            BOOST_TEST(receiver != destination);
            BOOST_TEST(snapshot[0].getIntegerValue() == receiver[0].getIntegerValue());
            BOOST_TEST(snapshot[1].getObjectValue() == receiver[1].getObjectValue());
            receiver[0].setIntegerValue(receiver[0].getIntegerValue() + active->getIntegerParameter(2));
            snapshot[0].setIntegerValue(-99);
            snapshot[1].freeObjectValue();
            snapshot[1].setStringValue("argument copy");
            type->freeValue(destination);
            type->copyValue(receiver, destination);
            destination[0].setIntegerValue(destination[0].getIntegerValue() + 1000);
        }, parameters, type, ""));
    const auto readId = owner->registerMethod(runtime, std::make_unique<NativeMethod>(
        "borrowedRead", type, [&](VirtualMachine* active) {
            ++readCalls;
            auto* receiver = &active->getStorageParameter(0, 0);
            BOOST_TEST(NomadString(receiver[1].getStringValue()) == "original");
            active->setIntegerResult(receiver[0].getIntegerValue());
        }, std::vector<Parameter>{}, runtime.getIntegerType(), ""));
    const auto scriptId = owner->registerMethod(runtime, std::make_unique<ScriptMethod>(
        "borrowedForward", type, parameters, type, "borrowed.nomad", ""));
    BOOST_REQUIRE(mutateId.isValid());
    BOOST_REQUIRE(readId.isValid());
    BOOST_REQUIRE(scriptId.isValid());
    auto* script = runtime.getScriptCallable(scriptId);
    const auto scriptDestination = script->registerVariable("destination", type);
    const auto parameterCallerId = runtime.registerScriptFunction("borrowedParameter", "borrowed.nomad", "");
    auto* parameterCaller = runtime.getScriptFunction(parameterCallerId);
    parameterCaller->setReturnType(runtime.getIntegerType());
    parameterCaller->addParameter("snapshot", type, "");
    parameterCaller->addParameter("delta", runtime.getIntegerType(), "");
    const auto parameterDestination = parameterCaller->registerVariable("destination", type);
    const auto callerId = runtime.registerScriptFunction("borrowedCaller", "borrowed.nomad", "");
    auto* caller = runtime.getScriptFunction(callerId);
    caller->setReturnType(runtime.getIntegerType());
    const auto sourceId = caller->registerVariable("source", type);
    const auto destinationId = caller->registerVariable("destination", type);
    const auto initializeId = runtime.registerNativeFunction("borrowedInitialize", [&](VirtualMachine* active) {
        auto* source = active->getLocalAddress(caller->getVariableOffset(sourceId));
        source[0].setIntegerValue(17);
        source[1].freeObjectValue();
        source[1].setStringValue("original");
    }, {}, runtime.getVoidType(), "");
    const auto argumentId = runtime.registerNativeFunction("borrowedArgument", [&](VirtualMachine* active) {
        ++argumentCalls;
        auto* source = active->getLocalAddress(caller->getVariableOffset(sourceId));
        BOOST_TEST(source[0].getIntegerValue() == (argumentCalls % 2 == 1 ? 17 : 18));
        source[0].setIntegerValue(source[0].getIntegerValue() + 1);
        active->setIntegerResult(3);
    }, {}, runtime.getIntegerType(), "");
    const auto inspectId = runtime.registerNativeFunction("borrowedInspect", [&](VirtualMachine* active) {
        auto* source = active->getLocalAddress(caller->getVariableOffset(sourceId));
        auto* destination = active->getLocalAddress(caller->getVariableOffset(destinationId));
        BOOST_TEST(source[0].getIntegerValue() == 22);
        BOOST_TEST(destination[0].getIntegerValue() == 1022);
        BOOST_TEST(source[1].getObjectValue() == destination[1].getObjectValue());
        BOOST_TEST(source[1].getObjectValue()->getReferenceCount() == 2U);
        BOOST_TEST(NomadString(source[1].getStringValue()) == "original");
    }, {}, runtime.getVoidType(), "");
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const auto resolveArguments = [&](
        ScriptCallableBase* function,
        const NomadString& snapshot,
        const NomadString& delta,
        const bool callArgument
    ) {
        auto arguments = std::make_unique<ArgumentList>();
        arguments->addExpressionArgument(1, 1, type,
            std::make_unique<IdentifierExpression>(nullptr, 1, 1, snapshot));
        if (callArgument) {
            arguments->addExpressionArgument(1, 1, runtime.getIntegerType(),
                std::make_unique<FunctionCallExpression>(nullptr, 1, 1, delta));
        } else {
            arguments->addExpressionArgument(1, 1, runtime.getIntegerType(),
                std::make_unique<IdentifierExpression>(nullptr, 1, 1, delta));
        }
        BOOST_REQUIRE(arguments->resolve(&context, function));
        return arguments;
    };
    auto scriptArguments = resolveArguments(script, "snapshot", "delta", false);
    auto parameterArguments = resolveArguments(parameterCaller, "snapshot", "delta", false);
    auto callerArguments = resolveArguments(caller, "source", "borrowedArgument", true);
    ArgumentList noArguments;
    IdentifierDefinition borrowedReceiver;
    borrowedReceiver.identifierType = IdentifierType::Receiver;
    IdentifierDefinition parameterReceiver;
    parameterReceiver.identifierType = IdentifierType::Parameter;
    parameterReceiver.parameterId = 0;
    IdentifierDefinition localReceiver;
    localReceiver.identifierType = IdentifierType::FunctionVariable;
    localReceiver.variableId = sourceId;
    const auto beforeInvalid = runtime.getInstructions().size();
    BOOST_CHECK_THROW(compiler->addStructMethodCall(mutateId, caller, callerArguments.get(), localReceiver), NomadBug);
    BOOST_CHECK_THROW(compiler->addStructMethodCall(readId, caller, &noArguments, localReceiver, destinationId), NomadBug);
    BOOST_CHECK_THROW(compiler->addStructMethodCall(readId, caller, &noArguments, borrowedReceiver), NomadBug);
    BOOST_TEST(runtime.getInstructions().size() == beforeInvalid);
    script->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(script);
    const auto nestedCallStart = runtime.getInstructions().size();
    compiler->addStructMethodCall(mutateId, script, scriptArguments.get(), borrowedReceiver, scriptDestination);
    BOOST_TEST(runtime.getInstructionId(runtime.getInstructions()[nestedCallStart + 2].fn).id ==
        runtime.getInstructionId(OpCodes::op_storage_parameter_address_push).id);
    BOOST_TEST(runtime.getInstructions()[nestedCallStart + 3].value.getIndexValue() == 3U);
    compiler->addStructMethodCall(readId, script, &noArguments, borrowedReceiver);
    type->emitCopyFunctionVariableToStorageParameter(*compiler, script->getVariableOffset(scriptDestination), 4, 0);
    compiler->addFunctionReturn(script);
    script->setFunctionEnd(runtime.getInstructions().size());
    parameterCaller->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(parameterCaller);
    const auto parameterCallStart = runtime.getInstructions().size();
    compiler->addStructMethodCall(mutateId, parameterCaller, parameterArguments.get(),
        parameterReceiver, parameterDestination);
    BOOST_TEST(runtime.getInstructionId(runtime.getInstructions()[parameterCallStart + 2].fn).id ==
        runtime.getInstructionId(OpCodes::op_parameter_address_push).id);
    BOOST_TEST(runtime.getInstructions()[parameterCallStart + 3].value.getIndexValue() == 1U);
    compiler->addStructMethodCall(readId, parameterCaller, &noArguments, parameterReceiver);
    compiler->addFunctionReturn(parameterCaller);
    parameterCaller->setFunctionEnd(runtime.getInstructions().size());
    caller->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(caller);
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(initializeId.id);
    compiler->addScriptFunctionCall(parameterCallerId, caller, callerArguments.get());
    const auto localCallStart = runtime.getInstructions().size();
    compiler->addStructMethodCall(scriptId, caller, callerArguments.get(), localReceiver, destinationId);
    BOOST_TEST(runtime.getInstructionId(runtime.getInstructions()[localCallStart].fn).id ==
        runtime.getInstructionId(OpCodes::op_local_address_push).id);
    BOOST_TEST(runtime.getInstructions()[localCallStart + 1].value.getIndexValue() ==
        caller->getVariableOffset(destinationId));
    BOOST_TEST(runtime.getInstructionId(runtime.getInstructions()[localCallStart + 2].fn).id ==
        runtime.getInstructionId(OpCodes::op_local_address_push).id);
    BOOST_TEST(runtime.getInstructions()[localCallStart + 3].value.getIndexValue() ==
        caller->getVariableOffset(sourceId));
    compiler->addOpCode(OpCodes::op_call_native_function);
    compiler->addId(inspectId.id);
    compiler->addStructMethodCall(readId, caller, &noArguments, localReceiver);
    compiler->addFunctionReturn(caller);
    caller->setFunctionEnd(runtime.getInstructions().size());
    compiler->linkFunctions();
    for (auto index = 0; index < 3; ++index) {
        RuntimeValue result;
        runtime.executeFunction(callerId, {}, result);
        BOOST_TEST(result.getIntegerValue() == 22);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(nativeCalls == 6);
    BOOST_TEST(readCalls == 9);
    BOOST_TEST(argumentCalls == 6);
    BOOST_REQUIRE(argumentId.isValid());
}

BOOST_AUTO_TEST_CASE(compiler_struct_method_calls_omit_empty_storage_and_preserve_void_and_reference_results) {
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    Runtime runtime;
    const auto emptyId = runtime.registerStruct("BorrowedEmpty");
    const auto textId = runtime.registerStruct("BorrowedText");
    const auto* empty = runtime.getTypeByName("BorrowedEmpty");
    const auto* text = runtime.getTypeByName("BorrowedText");
    BOOST_REQUIRE(runtime.registerProperty(textId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(emptyId);
    runtime.finalizeCompositeFieldLayout(textId);
    auto* emptyOwner = runtime.getTypeManager().getMutableCompositeType(emptyId);
    auto* textOwner = runtime.getTypeManager().getMutableCompositeType(textId);
    auto emptyCalls = 0;
    auto voidCalls = 0;
    const auto emptyMethodId = emptyOwner->registerMethod(runtime, std::make_unique<NativeMethod>(
        "borrowedEmptyResult", empty, [&](VirtualMachine*) { ++emptyCalls; },
        std::vector<Parameter>{}, empty, ""));
    const auto createId = emptyOwner->registerMethod(runtime, std::make_unique<NativeMethod>(
        "borrowedCreateText", empty, [](VirtualMachine* active) {
            auto& destination = active->getStorageParameter(0, 0);
            destination.freeObjectValue();
            destination.setStringValue("borrowed text");
        }, std::vector<Parameter>{}, text, ""));
    const auto voidId = textOwner->registerMethod(runtime, std::make_unique<NativeMethod>(
        "borrowedVoid", text, [&](VirtualMachine* active) {
            ++voidCalls;
            BOOST_TEST(NomadString(active->getStorageParameter(0, 0).getStringValue()) == "borrowed text");
        }, std::vector<Parameter>{}, runtime.getVoidType(), ""));
    const auto textMethodId = textOwner->registerMethod(runtime, std::make_unique<ScriptMethod>(
        "borrowedTextResult", text, std::vector<Parameter>{}, runtime.getStringType(), "empty.nomad", ""));
    const auto forwardId = emptyOwner->registerMethod(runtime, std::make_unique<ScriptMethod>(
        "borrowedEmptyForward", empty, std::vector<Parameter>{}, empty, "empty.nomad", ""));
    BOOST_REQUIRE(emptyMethodId.isValid());
    BOOST_REQUIRE(createId.isValid());
    BOOST_REQUIRE(voidId.isValid());
    BOOST_REQUIRE(textMethodId.isValid());
    BOOST_REQUIRE(forwardId.isValid());
    auto* forward = runtime.getScriptCallable(forwardId);
    const auto forwardDestination = forward->registerVariable("emptyDestination", empty);
    const auto callerId = runtime.registerScriptFunction("emptyBorrowedCaller", "empty.nomad", "");
    auto* caller = runtime.getScriptFunction(callerId);
    caller->setReturnType(runtime.getStringType());
    const auto emptyLocal = caller->registerVariable("emptyReceiver", empty);
    const auto emptyDestination = caller->registerVariable("emptyDestination", empty);
    const auto textLocal = caller->registerVariable("text", text);
    auto compiler = runtime.createCompiler();
    ArgumentList noArguments;
    IdentifierDefinition emptyReceiver;
    emptyReceiver.identifierType = IdentifierType::FunctionVariable;
    emptyReceiver.variableId = emptyLocal;
    IdentifierDefinition textReceiver;
    textReceiver.identifierType = IdentifierType::FunctionVariable;
    textReceiver.variableId = textLocal;
    IdentifierDefinition enclosingReceiver;
    enclosingReceiver.identifierType = IdentifierType::Receiver;
    forward->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(forward);
    const auto emptyNestedStart = runtime.getInstructions().size();
    compiler->addStructMethodCall(emptyMethodId, forward, &noArguments, enclosingReceiver, forwardDestination);
    BOOST_TEST(runtime.getInstructions().size() == emptyNestedStart + 2);
    compiler->addFunctionReturn(forward);
    forward->setFunctionEnd(runtime.getInstructions().size());
    auto* textMethod = runtime.getScriptCallable(textMethodId);
    textMethod->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(textMethod);
    runtime.getStringType()->emitLoadStorageParameter(*compiler, 0, 0, ExpressionTarget::Result);
    compiler->addFunctionReturn(textMethod);
    textMethod->setFunctionEnd(runtime.getInstructions().size());
    caller->setFunctionStart(runtime.getInstructions().size());
    compiler->addFunctionEntry(caller);
    const auto emptyStart = runtime.getInstructions().size();
    compiler->addStructMethodCall(forwardId, caller, &noArguments, emptyReceiver, emptyDestination);
    BOOST_TEST(runtime.getInstructions().size() == emptyStart + 2);
    BOOST_TEST(runtime.getInstructionId(runtime.getInstructions()[emptyStart].fn).id ==
        runtime.getInstructionId(OpCodes::op_call_script_method).id);
    const auto createStart = runtime.getInstructions().size();
    compiler->addStructMethodCall(createId, caller, &noArguments, emptyReceiver, textLocal);
    BOOST_TEST(runtime.getInstructions().size() == createStart + 5);
    BOOST_TEST(runtime.getInstructions()[createStart + 1].value.getIndexValue() == caller->getVariableOffset(textLocal));
    const auto voidStart = runtime.getInstructions().size();
    compiler->addStructMethodCall(voidId, caller, &noArguments, textReceiver);
    BOOST_TEST(runtime.getInstructions().size() == voidStart + 5);
    BOOST_TEST(runtime.getInstructionId(runtime.getInstructions()[voidStart + 4].fn).id ==
        runtime.getInstructionId(OpCodes::op_pop_1).id);
    compiler->addStructMethodCall(textMethodId, caller, &noArguments, textReceiver);
    compiler->addFunctionReturn(caller);
    caller->setFunctionEnd(runtime.getInstructions().size());
    compiler->linkFunctions();
    BOOST_TEST(caller->getVariableStorageSize() == 1U);
    for (auto index = 0; index < 3; ++index) {
        RuntimeValue result;
        runtime.executeFunction(callerId, {}, result);
        BOOST_TEST(NomadString(result.getStringValue()) == "borrowed text");
        result.freeObjectValue();
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    }
    BOOST_TEST(emptyCalls == 3);
    BOOST_TEST(voidCalls == 3);
}

BOOST_AUTO_TEST_CASE(compiled_struct_returns_support_nested_calls_assignments_arguments_and_discard) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto typeId = runtime.registerStruct("ReturnPayload");
    const auto nestedId = runtime.registerStruct("ReturnNested");
    const auto emptyId = runtime.registerStruct("ReturnEmpty");
    const auto* type = runtime.getTypeByName("ReturnPayload");
    const auto* nested = runtime.getTypeByName("ReturnNested");
    const auto* empty = runtime.getTypeByName("ReturnEmpty");
    BOOST_REQUIRE(runtime.registerProperty(nestedId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(typeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(typeId, "nested", nested).isValid());
    runtime.finalizeCompositeFieldLayout(typeId);
    runtime.finalizeCompositeFieldLayout(emptyId);
    auto calls = 0;
    auto emptyCalls = 0;
    const auto factoryId = runtime.registerNativeFunction("makePayload", [&](VirtualMachine* active) {
        ++calls;
        auto* destination = active->getParameter(1).getStorageAddress();
        destination[0] = active->getParameter(0);
        destination[1].freeObjectValue();
        destination[1].setStringValue("returned");
    }, {defParameter("number", runtime.getIntegerType(), "")}, type, "");
    BOOST_REQUIRE(factoryId.isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("makeEmpty", [&](VirtualMachine*) {
        ++emptyCalls;
    }, {}, empty, "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("readPayload", [](VirtualMachine* active) {
        const auto value = active->getParameterStorage(0, 2);
        BOOST_TEST(NomadString(value[1].getStringValue()) == "returned");
        active->setIntegerResult(value[0].getIntegerValue());
    }, {defParameter("payload", type, "")}, runtime.getIntegerType(), "").isValid());
    fixture.addFunction("main",
        "fun identity value:ReturnPayload\nreturn value\nend\n"
        "fun forward value:ReturnPayload\nreturn identity value\nend\n"
        "fun localReturn value:ReturnPayload\ncopy = forward value\nreturn copy\nend\n"
        "fun replace value:ReturnPayload\nvalue = makePayload 31\nreturn value\nend\n"
        "fun choose value:ReturnPayload flag:bool\nif flag\nreturn localReturn value\nend\nreturn replace value\nend\n"
        "fun emptyReturn\nreturn makeEmpty\nend\n"
        "original = makePayload 17\ncopy = forward original\n"
        "copy = choose original false\n"
        "identity original\nmakePayload 99\n"
        "empty = emptyReturn\nemptyReturn\n"
        "return (readPayload original) + (readPayload copy) + (readPayload (choose original true))");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* forward = runtime.getScriptFunction(runtime.getFunctionId("forward"));
    BOOST_REQUIRE(forward != nullptr);
    BOOST_TEST(forward->getParameterStorageSize() == 2U);
    BOOST_REQUIRE(forward->getCallStorageLayout().returnOffset.has_value());
    BOOST_TEST(*forward->getCallStorageLayout().returnOffset == 2U);
    BOOST_TEST(forward->getVariableCount() == 0U);
    BOOST_TEST(forward->getVariableStorageSize() == 0U);
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 65);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    }
    BOOST_TEST(calls == 9);
    BOOST_TEST(emptyCalls == 6);
    VirtualMachine machine(&runtime);
    const auto* identity = runtime.getScriptFunction(runtime.getFunctionId("identity"));
    BOOST_CHECK_THROW(machine.run(identity, {RuntimeValue{}}), VirtualMachineException);
    BOOST_CHECK_THROW(machine.runNativeFunction(factoryId, {RuntimeValue(NomadInteger{1})}), VirtualMachineException);
    const Interpreter interpreterEvaluator(machine);
    InterpreterContext interpreter(interpreterEvaluator);
    runtime.executeNativeFunction(factoryId, {RuntimeValue(NomadInteger{1})}, interpreter);
    BOOST_REQUIRE(interpreter.hasError());
    BOOST_TEST(interpreter.getResult() == nullptr);
    BOOST_TEST(calls == 9);
}

BOOST_AUTO_TEST_CASE(forwarded_native_returns_preserve_destinations_reentry_order_and_assignment_staging) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto typeId = runtime.registerStruct("ForwardedPayload");
    const auto* type = runtime.getTypeByName("ForwardedPayload");
    BOOST_REQUIRE(runtime.registerProperty(typeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(typeId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(typeId);
    const auto ownerId = runtime.registerClass("ForwardingOwner", ClassMode::Closed);
    const auto* ownerType = runtime.getTypeByName("ForwardingOwner")->asClassType();
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "payload", type).isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "makeForwardingOwner",
        [ownerType](VirtualMachine* active) {
            active->getResult().setObjectValue(new ScriptObject(ownerType));
        },
        {},
        ownerType,
        ""
    ).isValid());
    std::vector<NomadInteger> calls;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "argument",
        [&](VirtualMachine* active) {
            const auto value = active->getIntegerParameter(0);
            calls.push_back(value);
            active->setIntegerResult(value);
        },
        {defParameter("value", runtime.getIntegerType(), "")},
        runtime.getIntegerType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "makeForwardedPayload",
        [&](VirtualMachine* active) {
            calls.push_back(100);
            BOOST_TEST(active->getIntegerParameter(1) == 2);
            BOOST_TEST(active->getIntegerParameter(2) == 3);
            auto* destination = active->getParameter(3).getStorageAddress();
            auto* owner = static_cast<ScriptObject*>(active->getParameter(0).getObjectValue());
            const auto stored = owner->getFieldStorage(0);
            BOOST_TEST(destination != stored.data());
            BOOST_TEST(active->getEnclosingParameterStorage(1, 1)[0].getStorageAddress() == destination);
            const auto* success = runtime.getScriptFunction(runtime.getFunctionId("success"));
            active->run(success, {});
            BOOST_TEST(active->getIntegerResult() == 42);
            BOOST_TEST(active->getParameter(3).getStorageAddress() == destination);
            BOOST_TEST(active->getEnclosingParameterStorage(1, 1)[0].getStorageAddress() == destination);
            destination[0].setIntegerValue(17);
            destination[1].freeObjectValue();
            destination[1].setStringValue("forwarded");
            BOOST_TEST(stored[0].getIntegerValue() == 5);
        },
        {
            defParameter("owner", ownerType, ""),
            defParameter("first", runtime.getIntegerType(), ""),
            defParameter("second", runtime.getIntegerType(), "")
        },
        type,
        ""
    ).isValid());
    fixture.addFunction("success", "return 42");
    fixture.addFunction("main",
        "fun relay owner:ForwardingOwner\n"
        "return makeForwardedPayload owner (argument 2) (argument 3)\nend\n"
        "fun relayAgain owner:ForwardingOwner\nreturn relay owner\nend\n"
        "owner = makeForwardingOwner\nowner.payload.number = 5\n"
        "owner.payload = relayAgain owner\n"
        "assert owner.payload.text == \"forwarded\"\nreturn owner.payload.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    for (const auto* name : {"relay", "relayAgain"}) {
        const auto* wrapper = runtime.getScriptFunction(runtime.getFunctionId(name));
        BOOST_REQUIRE(wrapper != nullptr);
        BOOST_TEST(wrapper->getVariableCount() == 0U);
        BOOST_TEST(wrapper->getVariableStorageSize() == 0U);
        std::ostringstream dump;
        dumpInstructions(runtime, *wrapper, dump);
        BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_address_push) != NomadString::npos);
        BOOST_TEST(dump.str().find(OpCodes::op_local_address_push) == NomadString::npos);
        BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_set) == NomadString::npos);
    }
    const std::vector<NomadInteger> expectedCalls{3, 2, 100};
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        calls.clear();
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 17);
        BOOST_TEST(calls == expectedCalls, boost::test_tools::per_element());
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(native_construction_returns_use_forwarded_storage_without_exposing_assignment_targets) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto typeId = runtime.registerStruct("ConstructedReturn");
    const auto* type = runtime.getTypeByName("ConstructedReturn");
    BOOST_REQUIRE(runtime.registerProperty(typeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(typeId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(typeId);
    const auto ownerId = runtime.registerClass("ConstructionOwner", ClassMode::Closed);
    const auto* ownerType = runtime.getTypeByName("ConstructionOwner")->asClassType();
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "payload", type).isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "makeConstructionOwner",
        [ownerType](VirtualMachine* active) {
            active->getResult().setObjectValue(new ScriptObject(ownerType));
        },
        {},
        ownerType,
        ""
    ).isValid());
    std::vector<NomadInteger> calls;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "recordConstruction",
        [&](VirtualMachine* active) {
            const auto value = active->getIntegerParameter(0);
            calls.push_back(value);
            active->setIntegerResult(value);
        },
        {defParameter("value", runtime.getIntegerType(), "")},
        runtime.getIntegerType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeStructConstructor(typeId, {
        .parameters = {
            {ownerId, "owner", ""},
            {runtime.getIntegerType()->getId(), "first", ""},
            {runtime.getIntegerType()->getId(), "second", ""}
        },
        .function = [&](VirtualMachine* active) {
            calls.push_back(100);
            BOOST_TEST(active->getIntegerParameter(1) == 2);
            BOOST_TEST(active->getIntegerParameter(2) == 3);
            auto* destination = active->getParameter(3).getStorageAddress();
            BOOST_TEST(active->getEnclosingParameterStorage(1, 1)[0].getStorageAddress() == destination);
            auto* owner = static_cast<ScriptObject*>(active->getParameter(0).getObjectValue());
            const auto stored = owner->getFieldStorage(0);
            BOOST_TEST(destination != stored.data());
            BOOST_TEST(destination[0].getIntegerValue() == 0);
            BOOST_TEST(destination[1].getStringObject()->getString().empty());
            const auto* success = runtime.getScriptFunction(runtime.getFunctionId("constructionSuccess"));
            active->run(success, {});
            BOOST_TEST(active->getIntegerResult() == 42);
            BOOST_TEST(active->getParameter(3).getStorageAddress() == destination);
            destination[0].setIntegerValue(17);
            destination[1].freeObjectValue();
            destination[1].setStringValue("constructed");
            BOOST_TEST(stored[0].getIntegerValue() == 5);
            BOOST_TEST(stored[1].getStringObject()->getString() == "previous");
        }
    }).isValid());
    const auto emptyId = runtime.registerStruct("EmptyConstructionReturn");
    runtime.finalizeCompositeFieldLayout(emptyId);
    BOOST_REQUIRE(runtime.registerNativeStructConstructor(emptyId, {
        .parameters = {{runtime.getIntegerType()->getId(), "value", ""}},
        .function = [&](VirtualMachine* active) {
            calls.push_back(active->getIntegerParameter(0));
        }
    }).isValid());
    fixture.addFunction("constructionSuccess", "return 42");
    fixture.addFunction("main",
        "fun create owner:ConstructionOwner\n"
        "return new ConstructedReturn owner (recordConstruction 2) (recordConstruction 3)\nend\n"
        "fun emptyConstruction\nreturn new EmptyConstructionReturn (recordConstruction 4)\nend\n"
        "owner = makeConstructionOwner\n"
        "owner.payload.number = 5\nowner.payload.text = \"previous\"\n"
        "owner.payload = create owner\n"
        "assert owner.payload.text == \"constructed\"\n"
        "empty = emptyConstruction\nemptyConstruction\n"
        "return owner.payload.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto* create = runtime.getScriptFunction(runtime.getFunctionId("create"));
    BOOST_REQUIRE(create != nullptr);
    BOOST_TEST(create->getVariableCount() == 0U);
    std::ostringstream dump;
    dumpInstructions(runtime, *create, dump);
    BOOST_TEST(dump.str().find(OpCodes::op_create_native_struct) != NomadString::npos);
    BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_address_push) != NomadString::npos);
    BOOST_TEST(dump.str().find(OpCodes::op_local_address_push) == NomadString::npos);
    BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_set) == NomadString::npos);
    const auto* empty = runtime.getScriptFunction(runtime.getFunctionId("emptyConstruction"));
    BOOST_REQUIRE(empty != nullptr);
    BOOST_TEST(empty->getVariableCount() == 0U);
    BOOST_TEST(!empty->getCallStorageLayout().returnOffset.has_value());
    std::ostringstream emptyDump;
    dumpInstructions(runtime, *empty, emptyDump);
    BOOST_TEST(emptyDump.str().find(OpCodes::op_storage_parameter_address_push) == NomadString::npos);
    BOOST_TEST(emptyDump.str().find(OpCodes::op_create_native_struct) != NomadString::npos);
    const std::vector<NomadInteger> expectedCalls{3, 2, 100, 4, 4, 4, 4};
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        calls.clear();
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 17);
        BOOST_TEST(calls == expectedCalls, boost::test_tools::per_element());
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(native_method_return_forwarding_preserves_borrowed_storage_order_reentry_and_staging) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto typeId = runtime.registerStruct("MethodReturnPayload");
    const auto* type = runtime.getTypeByName("MethodReturnPayload");
    BOOST_REQUIRE(runtime.registerProperty(typeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(typeId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(typeId);
    const auto ownerId = runtime.registerClass("MethodReturnOwner", ClassMode::Closed);
    const auto* ownerType = runtime.getTypeByName("MethodReturnOwner")->asClassType();
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "payload", type).isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "makeMethodOwner",
        [ownerType](VirtualMachine* active) {
            active->getResult().setObjectValue(new ScriptObject(ownerType));
        },
        {},
        ownerType,
        ""
    ).isValid());
    std::vector<NomadInteger> calls;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "selectMethodOwner",
        [&](VirtualMachine* active) {
            calls.push_back(1);
            active->getResult().copyObjectValue(active->getParameter(0));
        },
        {defParameter("owner", ownerType, "")},
        ownerType,
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "methodArgument",
        [&](VirtualMachine* active) {
            const auto number = active->getIntegerParameter(0);
            calls.push_back(number);
            active->setIntegerResult(number);
        },
        {defParameter("number", runtime.getIntegerType(), "")},
        runtime.getIntegerType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.addMethod(typeId, {
        .name = "copied",
        .parameters = {
            {ownerId, "owner", ""},
            {runtime.getIntegerType()->getId(), "first", ""},
            {runtime.getIntegerType()->getId(), "second", ""}
        },
        .returnTypeId = typeId,
        .function = [&](VirtualMachine* active) {
            calls.push_back(100);
            BOOST_TEST(active->getIntegerParameter(1) == 2);
            BOOST_TEST(active->getIntegerParameter(2) == 3);
            auto* receiver = active->getParameter(3).getStorageAddress();
            auto* destination = active->getParameter(4).getStorageAddress();
            auto* owner = static_cast<ScriptObject*>(active->getParameter(0).getObjectValue());
            BOOST_TEST(receiver == owner->getFieldStorage(0).data());
            BOOST_TEST(receiver != destination);
            BOOST_TEST(active->getEnclosingParameterStorage(1, 1)[0].getStorageAddress() == destination);
            const auto* success = runtime.getScriptFunction(runtime.getFunctionId("methodSuccess"));
            active->run(success, {});
            BOOST_TEST(active->getIntegerResult() == 42);
            BOOST_TEST(active->getParameter(3).getStorageAddress() == receiver);
            BOOST_TEST(active->getParameter(4).getStorageAddress() == destination);
            const auto referencesBeforeCopy = receiver[1].getObjectValue()->getReferenceCount();
            type->freeValue(destination);
            type->copyValue(receiver, destination);
            destination[0].setIntegerValue(17);
            BOOST_TEST(receiver[0].getIntegerValue() == 5);
            BOOST_TEST(receiver[1].getStringObject()->getReferenceCount() == referencesBeforeCopy + 1U);
        }
    }).isValid());
    fixture.addFunction("methodSuccess", "return 42");
    fixture.addFunction("main",
        "fun forwardMethod owner:MethodReturnOwner\n"
        "return (selectMethodOwner owner).payload.copied owner (methodArgument 2) (methodArgument 3)\nend\n"
        "owner = makeMethodOwner\nowner.payload.number = 5\nowner.payload.text = \"owned\"\n"
        "owner.payload = forwardMethod owner\n"
        "assert owner.payload.text == \"owned\"\nreturn owner.payload.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto* wrapper = runtime.getScriptFunction(runtime.getFunctionId("forwardMethod"));
    BOOST_REQUIRE(wrapper != nullptr);
    BOOST_TEST(wrapper->getVariableCount() == 0U);
    BOOST_TEST(wrapper->getVariableStorageSize() == 0U);
    std::ostringstream dump;
    dumpInstructions(runtime, *wrapper, dump);
    BOOST_TEST(dump.str().find(OpCodes::op_call_native_method) != NomadString::npos);
    BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_address_push) != NomadString::npos);
    BOOST_TEST(dump.str().find(OpCodes::op_stored_property_address_push) != NomadString::npos);
    BOOST_TEST(dump.str().find(OpCodes::op_local_address_push) == NomadString::npos);
    BOOST_TEST(dump.str().find(OpCodes::op_storage_parameter_set) == NomadString::npos);
    const std::vector<NomadInteger> expectedCalls{1, 3, 2, 100};
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        calls.clear();
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 17);
        BOOST_TEST(calls == expectedCalls, boost::test_tools::per_element());
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(source_struct_methods_borrow_receivers_and_stage_owned_results) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto typeId = runtime.registerStruct("SourcePoint");
    const auto emptyId = runtime.registerStruct("SourceEmpty");
    const auto* type = runtime.getTypeByName("SourcePoint");
    const auto* empty = runtime.getTypeByName("SourceEmpty");
    BOOST_REQUIRE(runtime.registerProperty(typeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(typeId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(typeId);
    runtime.finalizeCompositeFieldLayout(emptyId);
    BOOST_REQUIRE(runtime.registerNativeFunction("makeSourcePoint", [](VirtualMachine* active) {
        auto* destination = active->getParameter(1).getStorageAddress();
        destination[0] = active->getParameter(0);
        destination[1].freeObjectValue();
        destination[1].setStringValue("original");
    }, {defParameter("number", runtime.getIntegerType(), "")}, type, "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("makeSourceEmpty", [](VirtualMachine*) {},
        {}, empty, "").isValid());
    auto shiftCalls = 0;
    const auto shiftId = runtime.addMethod(typeId, {
        .name = "shiftSourcePoint",
        .parameters = {{runtime.getIntegerType()->getId(), "delta", ""}},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [&](VirtualMachine* active) {
            ++shiftCalls;
            auto& number = active->getStorageParameter(1, 0);
            number.setIntegerValue(number.getIntegerValue() + active->getIntegerParameter(0));
        },
        .documentation = ""
    });
    BOOST_REQUIRE(shiftId.isValid());
    BOOST_REQUIRE(runtime.addMethod(typeId, {
        .name = "readSourcePoint",
        .parameters = {},
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [](VirtualMachine* active) {
            active->setIntegerResult(active->getStorageParameter(0, 0).getIntegerValue());
        },
        .documentation = ""
    }).isValid());
    BOOST_REQUIRE(runtime.addMethod(typeId, {
        .name = "copySourcePoint",
        .parameters = {},
        .returnTypeId = typeId,
        .function = [type](VirtualMachine* active) {
            auto* destination = active->getParameter(1).getStorageAddress();
            type->freeValue(destination);
            type->copyValue(&active->getStorageParameter(0, 0), destination);
        },
        .documentation = ""
    }).isValid());
    BOOST_REQUIRE(runtime.addMethod(typeId, {
        .name = "textSourcePoint",
        .parameters = {},
        .returnTypeId = runtime.getStringType()->getId(),
        .function = [](VirtualMachine* active) {
            active->getResult().copyObjectValue(active->getStorageParameter(0, 1));
        },
        .documentation = ""
    }).isValid());
    BOOST_REQUIRE(runtime.addMethod(typeId, {
        .name = "takeSourcePoint",
        .parameters = {{typeId, "value", ""}},
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [](VirtualMachine* active) {
            auto value = active->getParameterStorage(0, 2);
            value[0].setIntegerValue(-100);
            value[1].freeObjectValue();
            value[1].setStringValue("parameter copy");
            active->setIntegerResult(active->getStorageParameter(2, 0).getIntegerValue());
        },
        .documentation = ""
    }).isValid());
    auto emptyCalls = 0;
    BOOST_REQUIRE(runtime.addMethod(emptyId, {
        .name = "cloneSourceEmpty",
        .parameters = {},
        .returnTypeId = emptyId,
        .function = [&](VirtualMachine*) { ++emptyCalls; },
        .documentation = ""
    }).isValid());
    const auto forwardId = runtime.registerScriptMethod(typeId, "sourceForward",
        {defParameter("delta", runtime.getIntegerType(), "")}, "forward.nomad",
        "this.shiftSourcePoint delta\ncopy = this\nthis.takeSourcePoint this\nthis.copySourcePoint\nreturn this");
    const auto wrappedId = runtime.registerScriptMethod(typeId, "sourceWrapped",
        {defParameter("delta", runtime.getIntegerType(), "")}, "wrapped.nomad", "return this.sourceForward delta");
    const auto emptyForwardId = runtime.registerScriptMethod(emptyId, "emptySourceForward",
        {}, "emptyForward.nomad", "this.cloneSourceEmpty\nreturn this");
    BOOST_REQUIRE(forwardId.isValid());
    BOOST_REQUIRE(wrappedId.isValid());
    BOOST_REQUIRE(emptyForwardId.isValid());
    fixture.addFunction("main",
        "fun mutateCopy value:SourcePoint\nvalue.shiftSourcePoint 50\nreturn value.readSourcePoint\nend\n"
        "fun relay value:SourcePoint\nreturn value.sourceWrapped 2\nend\n"
        "value = makeSourcePoint 17\nvalue.shiftSourcePoint 3\n"
        "assert (mutateCopy value) == 70\nassert (value.readSourcePoint) == 20\n"
        "copy = value.copySourcePoint\ncopy.shiftSourcePoint 1\n"
        "returned = value.sourceWrapped 4\n"
        "assert (value.readSourcePoint) == 24\nassert (returned.readSourcePoint) == 24\n"
        "returned.shiftSourcePoint 2\n"
        "value = value.copySourcePoint\nvalue.copySourcePoint\n"
        "assert (value.takeSourcePoint (value.copySourcePoint)) == 24\n"
        "assert (value.textSourcePoint) == \"original\"\n"
        "other = relay value\nassert (other.readSourcePoint) == 26\n"
        "assert (value.readSourcePoint) == 24\n"
        "empty = makeSourceEmpty\nempty.emptySourceForward\nempty = empty.cloneSourceEmpty\n"
        "return (value.readSourcePoint) + (returned.readSourcePoint) + (copy.readSourcePoint)");
    fixture.getCompiler().registerPhaseHook([&](CompilerContext* context, const CompilationPhase phase) {
        if (phase != CompilationPhase::Parse) {
            return;
        }
        for (const auto id : {forwardId, wrappedId, emptyForwardId}) {
            auto* method = runtime.getScriptCallable(id);
            fixture.getCompiler().registerMemberCompilation(
                id, runtime.getFunctionId("main"), CompilationCallableTarget::Function);
            auto body = std::make_unique<FunctionNode>(0, 0);
            Tokenizer tokens(context, method->getPath(), method->getSourceCode());
            while (tokens.nextLine()) {
                if (auto statement = parser::parseLine(context, method, &tokens)) {
                    body->addStatement(std::move(statement));
                }
            }
            fixture.getCompiler().setFunctionBody(id, std::move(body));
        }
    });
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 71);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    }
    BOOST_TEST(emptyCalls == 6);
    BOOST_TEST(shiftCalls == 18);
    const auto* forward = runtime.getScriptCallable(forwardId);
    for (auto index = NomadIndex{0}; index < forward->getVariableCount(); ++index) {
        const auto variableId = VariableId{toNomadId(index)};
        BOOST_TEST(!forward->getVariableName(variableId).starts_with("$argument"));
    }
    VirtualMachine machine(&runtime);
    RuntimeValue receiver;
    BOOST_CHECK_THROW(machine.runNativeMethod(shiftId, {RuntimeValue(NomadInteger{1})}, receiver),
        VirtualMachineException);
    BOOST_CHECK_THROW(machine.runScriptMethod(forwardId, {RuntimeValue(NomadInteger{1})}, receiver),
        VirtualMachineException);
    BOOST_TEST(shiftCalls == 18);
}

BOOST_AUTO_TEST_CASE(native_methods_borrow_temporary_receivers_and_preserve_owned_results) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto typeId = runtime.registerStruct("NativeTemporary");
    const auto* type = runtime.getTypeByName("NativeTemporary");
    BOOST_REQUIRE(runtime.registerProperty(typeId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(typeId, "text", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(typeId);
    auto factories = 0;
    auto touches = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "makeNativeTemporary",
        [&](VirtualMachine* active) {
            ++factories;
            auto* destination = active->getParameter(1).getStorageAddress();
            destination[0] = active->getParameter(0);
            destination[1].freeObjectValue();
            destination[1].setStringValue("native temporary");
        },
        {defParameter("number", runtime.getIntegerType(), "")},
        type,
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.addMethod(typeId, {
        .name = "shift",
        .parameters = {{runtime.getIntegerType()->getId(), "amount", ""}},
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [](VirtualMachine* active) {
            auto& number = active->getStorageParameter(1, 0);
            number.setIntegerValue(number.getIntegerValue() + active->getIntegerParameter(0));
            active->setIntegerResult(number.getIntegerValue());
        }
    }).isValid());
    BOOST_REQUIRE(runtime.addMethod(typeId, {
        .name = "label",
        .parameters = {},
        .returnTypeId = runtime.getStringType()->getId(),
        .function = [](VirtualMachine* active) {
            active->getResult().copyObjectValue(active->getStorageParameter(0, 1));
        }
    }).isValid());
    BOOST_REQUIRE(runtime.addMethod(typeId, {
        .name = "copied",
        .parameters = {},
        .returnTypeId = typeId,
        .function = [type](VirtualMachine* active) {
            auto* destination = active->getParameter(1).getStorageAddress();
            type->freeValue(destination);
            type->copyValue(&active->getStorageParameter(0, 0), destination);
        }
    }).isValid());
    BOOST_REQUIRE(runtime.addMethod(typeId, {
        .name = "touch",
        .parameters = {},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [&](VirtualMachine*) { ++touches; }
    }).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "inspectTemporaryCopy",
        [](VirtualMachine* active) {
            const auto value = active->getParameterStorage(0, 2);
            BOOST_TEST(value[1].getStringObject()->getReferenceCount() == 2U);
            active->setIntegerResult(value[0].getIntegerValue());
        },
        {defParameter("value", type, "")},
        runtime.getIntegerType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "first = (makeNativeTemporary 4).shift 3\n"
        "text = (makeNativeTemporary 8).label\nassert text == \"native temporary\"\n"
        "copy = (makeNativeTemporary 11).copied\nassert (inspectTemporaryCopy copy) == 11\n"
        "(makeNativeTemporary 9).touch\n"
        "assert (((makeNativeTemporary 13).copied).shift 2) == 15\n"
        "return first + copy.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        factories = 0;
        touches = 0;
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 18);
        BOOST_TEST(factories == 5);
        BOOST_TEST(touches == 1);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(source_struct_methods_accept_temporary_receivers_but_reject_interpreter_storage) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto typeId = runtime.registerStruct("ReceiverChecks");
    const auto* type = runtime.getTypeByName("ReceiverChecks");
    runtime.finalizeCompositeFieldLayout(typeId);
    auto calls = 0;
    BOOST_REQUIRE(runtime.addMethod(typeId, {
        .name = "readReceiverCheck",
        .parameters = {},
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [&](VirtualMachine* active) { ++calls; active->setIntegerResult(1); },
        .documentation = ""
    }).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("makeReceiverCheck", [](VirtualMachine*) {},
        {}, type, "").isValid());
    fixture.addFunction("main", "return (makeReceiverCheck).readReceiverCheck");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    BOOST_TEST(calls == 0);
    const auto* main = runtime.getScriptFunction(runtime.getFunctionId("main"));
    BOOST_TEST(main->getVariableCount() == 1U);
    BOOST_TEST(main->getVariableStorageSize() == 0U);
    const auto outcome = fixture.execute("main");
    BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
    BOOST_TEST(outcome.integerValue == 1);
    BOOST_TEST(calls == 1);
    ScriptFunction caller(CallableId{}, "checks", "checks.nomad", "");
    caller.setReturnType(runtime.getVoidType());
    caller.registerVariable("value", type);
    CompilerContext context(&fixture.getCompiler());
    NativeMemberExpression expression(nullptr, 1, 1, "value.readReceiverCheck", "readReceiverCheck",
        std::make_unique<IdentifierExpression>(nullptr, 1, 1, "value"), ArgumentList{});
    BOOST_REQUIRE(expression.resolve(&context, &caller));
    const auto before = runtime.getInstructions().size();
    expression.compile(&fixture.getCompiler(), &caller, ExpressionTarget::Result);
    BOOST_TEST(runtime.getInstructions().size() == before + 2);
    VirtualMachine machine(&runtime);
    const Interpreter interpreterEvaluator(machine);
    InterpreterContext interpreter(interpreterEvaluator);
    expression.evaluate(interpreter);
    BOOST_REQUIRE(interpreter.hasError());
    BOOST_TEST(interpreter.getResult() == nullptr);
    BOOST_TEST(calls == 1);
    const auto scriptId = runtime.registerScriptMethod(typeId, "checkProperties", {}, "properties.nomad", "");
    auto* script = runtime.getScriptCallable(scriptId);
    BOOST_REQUIRE(script != nullptr);
    NativeMemberExpression property(nullptr, 1, 1, "this.number", "number",
        std::make_unique<IdentifierExpression>(nullptr, 1, 1, "this"), ArgumentList{});
    BOOST_TEST(!property.resolve(&context, script));
    const auto errorCount = context.getErrorCount();
    MemberAssignmentStatement assignment(1, 1,
        std::make_unique<NativeMemberExpression>(nullptr, 1, 1, "this.number", "number",
            std::make_unique<IdentifierExpression>(nullptr, 1, 1, "this"), ArgumentList{}),
        std::make_unique<IntegerLiteral>(nullptr, 1, 1, 3));
    BOOST_TEST(!assignment.resolve(&context, script));
    BOOST_TEST(context.getErrorCount() == errorCount + 1);
    const auto nestedId = runtime.registerScriptFunction("receiverCapture", "capture.nomad", "");
    auto* nested = runtime.getScriptFunction(nestedId);
    nested->setParentId(scriptId);
    IdentifierExpression capture(nullptr, 1, 1, "this");
    BOOST_CHECK_THROW(capture.resolve(&context, nested), CompilerException);
    BOOST_TEST(nested->getParameterCount() == 0U);
}

BOOST_AUTO_TEST_CASE(source_struct_properties_share_nested_local_parameter_and_borrowed_storage) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto emptyId = runtime.registerStruct("PropertyEmpty");
    const auto leafId = runtime.registerStruct("PropertyLeaf");
    const auto pairId = runtime.registerStruct("PropertyPair");
    const auto* empty = runtime.getTypeByName("PropertyEmpty");
    const auto* leaf = runtime.getTypeByName("PropertyLeaf");
    const auto* pair = runtime.getTypeByName("PropertyPair");
    BOOST_REQUIRE(runtime.registerProperty(leafId, "nothing", empty).isValid());
    BOOST_REQUIRE(runtime.registerProperty(leafId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(leafId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(leafId, "tail", empty).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "prefix", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "left", leaf).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "middle", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "right", leaf).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "empty", empty).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pairId, "suffix", runtime.getIntegerType()).isValid());
    runtime.finalizeCompositeFieldLayout(pairId);
    BOOST_REQUIRE(pair->getSize() == 7U);
    BOOST_REQUIRE(runtime.registerNativeFunction("makePropertyPair", [](VirtualMachine* active) {
        auto* destination = active->getParameter(0).getStorageAddress();
        destination[0].setIntegerValue(101);
        destination[1].setIntegerValue(11);
        destination[2].freeObjectValue();
        destination[2].setStringValue("left");
        destination[3].freeObjectValue();
        destination[3].setStringValue("middle");
        destination[4].setIntegerValue(22);
        destination[5].freeObjectValue();
        destination[5].setStringValue("right");
        destination[6].setIntegerValue(303);
    }, {}, pair, "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("makePropertyLeaf", [](VirtualMachine* active) {
        auto* destination = active->getParameter(1).getStorageAddress();
        destination[0] = active->getParameter(0);
        destination[1].freeObjectValue();
        destination[1].setStringValue("made");
    }, {defParameter("number", runtime.getIntegerType(), "")}, leaf, "").isValid());
    auto emptyCalls = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction("makePropertyEmpty", [&](VirtualMachine*) {
        ++emptyCalls;
    }, {}, empty, "").isValid());
    auto emptyTouches = 0;
    BOOST_REQUIRE(runtime.addMethod(emptyId, {
        .name = "touchPropertyEmpty",
        .parameters = {},
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [&](VirtualMachine* active) {
            ++emptyTouches;
            active->setIntegerResult(123);
        },
        .documentation = ""
    }).isValid());
    const auto leafMethod = runtime.registerScriptMethod(leafId, "copyPropertyLeaf",
        {defParameter("delta", runtime.getIntegerType(), "")}, "leaf.nomad",
        "this.number = this.number + delta\nthis.text = this.text\nreturn this");
    const auto pairMethod = runtime.registerScriptMethod(pairId, "mutatePropertyPair",
        {defParameter("delta", runtime.getIntegerType(), "")}, "pair.nomad",
        "this.left.number = this.left.number + delta\nthis.left.text = this.right.text\n"
        "this.right = this.left\nthis.left = this.left\nthis.left = this.right.copyPropertyLeaf 1\n"
        "this.empty = makePropertyEmpty\nreturn this.left");
    const auto prepareMethod = runtime.registerScriptMethod(pairId, "preparePropertyAssignment",
        {}, "prepare.nomad", "this.left.number = 999\nreturn 45");
    BOOST_REQUIRE(leafMethod.isValid());
    BOOST_REQUIRE(pairMethod.isValid());
    BOOST_REQUIRE(prepareMethod.isValid());
    fixture.addFunction("main",
        "fun changePropertyParameter value:PropertyPair delta:int\n"
        "assert value.prefix == 101\nassert value.suffix == 303\n"
        "value.left.number = value.left.number + delta\nvalue.left.text = value.right.text\n"
        "value.right = value.left\nvalue.right = value.right.copyPropertyLeaf 1\nreturn value.right\nend\n"
        "fun takePropertyLeaf value:PropertyLeaf\nvalue.number = value.number + 5\nreturn value\nend\n"
        "fun takePropertyEmpty value:PropertyEmpty\nreturn 7\nend\n"
        "value = makePropertyPair\n"
        "value.left.number = value.preparePropertyAssignment\nassert value.left.number == 45\n"
        "value.left.number = 30\nvalue.left.text = value.left.text\nvalue.middle = value.left.text\n"
        "value.right = value.left\nvalue.left = makePropertyLeaf 7\n"
        "assert value.left.number == 7\nassert value.left.text == \"made\"\n"
        "value.left = value.right\nvalue.left = value.left.copyPropertyLeaf 2\n"
        "copy = value.left\ncopy.number = 100\ncopy.text = \"copy\"\n"
        "isolated = changePropertyParameter value 3\n"
        "assert isolated.number == 36\nassert value.left.number == 32\nassert value.right.number == 30\n"
        "returned = value.mutatePropertyPair 5\n"
        "assert returned.number == 38\nassert returned.text == \"left\"\n"
        "relayed = takePropertyLeaf value.left\nassert relayed.number == 43\n"
        "assert value.left.number == 38\nassert value.right.number == 38\n"
        "assert value.middle == \"left\"\nassert value.prefix == 101\nassert value.suffix == 303\n"
        "assert copy.number == 100\nassert copy.text == \"copy\"\n"
        "value.empty = makePropertyEmpty\nvalue.left.nothing = makePropertyEmpty\n"
        "value.left.tail = value.empty\nvalue.empty = value.empty\n"
        "assert (takePropertyEmpty value.empty) == 7\n"
        "assert (value.empty.touchPropertyEmpty) == 123\n"
        "assert (value.left.tail.touchPropertyEmpty) == 123\nvalue.empty\n"
        "return value.left.number + value.right.number");
    fixture.getCompiler().registerPhaseHook([&](CompilerContext* context, const CompilationPhase phase) {
        if (phase != CompilationPhase::Parse) {
            return;
        }
        for (const auto id : {leafMethod, pairMethod, prepareMethod}) {
            auto* method = runtime.getScriptCallable(id);
            fixture.getCompiler().registerMemberCompilation(
                id, runtime.getFunctionId("main"), CompilationCallableTarget::Function);
            auto body = std::make_unique<FunctionNode>(0, 0);
            Tokenizer tokens(context, method->getPath(), method->getSourceCode());
            while (tokens.nextLine()) {
                if (auto statement = parser::parseLine(context, method, &tokens)) {
                    body->addStatement(std::move(statement));
                }
            }
            fixture.getCompiler().setFunctionBody(id, std::move(body));
        }
    });
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 76);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    }
    BOOST_TEST(emptyCalls == 9);
    BOOST_TEST(emptyTouches == 6);
    const auto* main = runtime.getScriptFunction(runtime.getFunctionId("main"));
    for (auto index = NomadIndex{0}; index < main->getVariableCount(); ++index) {
        BOOST_TEST(!main->getVariableName(VariableId{toNomadId(index)}).starts_with("$argument"));
    }
}

BOOST_AUTO_TEST_CASE(struct_property_storage_resolves_parameter_subvalue_anchor_operands) {
    Runtime runtime;
    const auto leafId = runtime.registerStruct("OffsetLeaf");
    const auto rootId = runtime.registerStruct("OffsetRoot");
    const auto* leaf = runtime.getTypeByName("OffsetLeaf");
    const auto* root = runtime.getTypeByName("OffsetRoot");
    BOOST_REQUIRE(runtime.registerProperty(leafId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(leafId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(rootId, "prefix", runtime.getIntegerType()).isValid());
    const auto leftId = runtime.registerProperty(rootId, "left", leaf);
    const auto rightId = runtime.registerProperty(rootId, "right", leaf);
    BOOST_REQUIRE(leftId.isValid());
    BOOST_REQUIRE(rightId.isValid());
    BOOST_REQUIRE(runtime.registerProperty(rootId, "suffix", runtime.getIntegerType()).isValid());
    runtime.finalizeCompositeFieldLayout(rootId);
    ScriptFunction caller(CallableId{}, "offsets", "offsets.nomad", "");
    caller.setReturnType(runtime.getVoidType());
    caller.addParameter("value", root, "");
    const auto destination = caller.registerVariable("destination", leaf);
    IdentifierDefinition definition;
    definition.identifierType = IdentifierType::Parameter;
    definition.valueType = root;
    definition.parameterId = 0;
    const auto left = StorageLocation(definition).getProperty(*runtime.getProperty(leftId));
    const auto right = StorageLocation(definition).getProperty(*runtime.getProperty(rightId));
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    auto start = instructions.size();
    left.emitAddress(compiler, &caller);
    BOOST_TEST(runtime.getInstructionId(instructions[start].fn).id ==
        runtime.getInstructionId(OpCodes::op_parameter_address_push).id);
    BOOST_TEST(instructions[start + 1].value.getIndexValue() == 4U);
    start = instructions.size();
    right.emitAddress(compiler, &caller);
    BOOST_TEST(instructions[start + 1].value.getIndexValue() == 2U);
    start = instructions.size();
    left.emitCopyToLocal(compiler, &caller, destination);
    BOOST_TEST(instructions[start + 1].value.getIndexValue() == 4U);
    BOOST_TEST(instructions[start + 5].value.getIndexValue() == 3U);
    start = instructions.size();
    left.emitCopyFromLocal(compiler, &caller, destination);
    BOOST_TEST(instructions[start + 3].value.getIndexValue() == 4U);
    BOOST_TEST(instructions[start + 9].value.getIndexValue() == 3U);
    start = instructions.size();
    BOOST_CHECK_THROW(left.emitLoad(compiler, &caller, ExpressionTarget::Result), NomadException);
    BOOST_TEST(instructions.size() == start);
}

BOOST_AUTO_TEST_CASE(source_struct_properties_preserve_class_reference_semantics_and_ownership) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto actorId = runtime.registerClass("PropertyActor", ClassMode::Closed);
    const auto holderId = runtime.registerStruct("PropertyHolder");
    const auto* actor = runtime.getTypeByName("PropertyActor")->asClassType();
    const auto* holder = runtime.getTypeByName("PropertyHolder");
    BOOST_REQUIRE(runtime.registerProperty(actorId, "value", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(holderId, "actor", actor).isValid());
    runtime.finalizeCompositeFieldLayout(actorId);
    runtime.finalizeCompositeFieldLayout(holderId);
    BOOST_REQUIRE(runtime.registerNativeFunction("makePropertyActor", [actor](VirtualMachine* active) {
        active->getResult().setObjectValue(new ScriptObject(actor));
    }, {}, actor, "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("makePropertyHolder", [actor](VirtualMachine* active) {
        auto* destination = active->getParameter(0).getStorageAddress();
        destination[0].freeObjectValue();
        destination[0].setObjectValue(new ScriptObject(actor));
    }, {}, holder, "").isValid());
    const auto methodId = runtime.registerScriptMethod(holderId, "replacePropertyActor",
        {defParameter("number", runtime.getIntegerType(), "")}, "actor.nomad",
        "this.actor = this.actor\nthis.actor = makePropertyActor\nthis.actor.value = number\nreturn this.actor");
    BOOST_REQUIRE(methodId.isValid());
    fixture.addFunction("main",
        "class PropertyBox\nvalue = makePropertyHolder\nend\n"
        "fun mutatePropertyHolder copy:PropertyHolder\n"
        "copy.actor.value = 23\ncopy.actor = makePropertyActor\ncopy.actor.value = 71\nreturn copy.actor\nend\n"
        "value = makePropertyHolder\nvalue.actor.value = 12\nvalue.actor = value.actor\n"
        "copy = value\ncopy.actor.value = 19\nassert value.actor.value == 19\n"
        "returned = mutatePropertyHolder value\nassert returned.value == 71\nassert value.actor.value == 23\n"
        "copy.actor = makePropertyActor\ncopy.actor.value = 31\n"
        "assert value.actor.value == 23\nassert copy.actor.value == 31\n"
        "returned = value.replacePropertyActor 41\nassert returned.value == 41\n"
        "boxed = new PropertyBox\nboxed.value.actor.value = 11\n"
        "held = boxed.value\nheld.actor.value = 13\nassert boxed.value.actor.value == 13\n"
        "boxed.value.actor = makePropertyActor\nboxed.value.actor.value = 29\nassert held.actor.value == 13\n"
        "boxed.value = held\nboxed.value = boxed.value\nassert boxed.value.actor.value == 13\n"
        "return value.actor.value");
    fixture.getCompiler().registerPhaseHook([&](CompilerContext* context, const CompilationPhase phase) {
        if (phase != CompilationPhase::Parse) {
            return;
        }
        auto* method = runtime.getScriptCallable(methodId);
        fixture.getCompiler().registerMemberCompilation(
            methodId, runtime.getFunctionId("main"), CompilationCallableTarget::Function);
        auto body = std::make_unique<FunctionNode>(0, 0);
        Tokenizer tokens(context, method->getPath(), method->getSourceCode());
        while (tokens.nextLine()) {
            if (auto statement = parser::parseLine(context, method, &tokens)) {
                body->addStatement(std::move(statement));
            }
        }
        fixture.getCompiler().setFunctionBody(methodId, std::move(body));
    });
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 41);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(source_object_owned_struct_values_and_methods_retain_evaluated_owners) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto emptyId = runtime.registerStruct("OwnedEmpty");
    const auto pointId = runtime.registerStruct("OwnedPoint");
    const auto outerId = runtime.registerStruct("OwnedOuter");
    const auto ownerId = runtime.registerClass("OwnedReceiver", ClassMode::Closed);
    const auto emptyOwnerId = runtime.registerClass("OnlyEmptyOwnedReceiver", ClassMode::Closed);
    const auto* empty = runtime.getTypeByName("OwnedEmpty");
    const auto* point = runtime.getTypeByName("OwnedPoint");
    const auto* outer = runtime.getTypeByName("OwnedOuter");
    const auto* owner = runtime.getTypeByName("OwnedReceiver")->asClassType();
    const auto* emptyOwner = runtime.getTypeByName("OnlyEmptyOwnedReceiver")->asClassType();
    BOOST_REQUIRE(runtime.registerProperty(pointId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(pointId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "before", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "point", point).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "after", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "prefix", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "outer", outer).isValid());
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "empty", empty).isValid());
    BOOST_REQUIRE(runtime.registerProperty(ownerId, "suffix", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(emptyOwnerId, "empty", empty).isValid());
    runtime.finalizeCompositeFieldLayout(ownerId);
    runtime.finalizeCompositeFieldLayout(emptyOwnerId);
    const auto createOwner = [&](const NomadInteger number) {
        auto* object = new ScriptObject(owner);
        object->getStorageValue(0).setIntegerValue(101);
        object->getStorageValue(1).setIntegerValue(202);
        object->getStorageValue(2).setIntegerValue(number);
        object->getStorageValue(3).setStringValue("kept");
        object->getStorageValue(4).setIntegerValue(404);
        object->getStorageValue(5).setIntegerValue(505);
        return object;
    };
    BOOST_REQUIRE(runtime.registerNativeFunction("makeOwnedReceiver", [&](VirtualMachine* active) {
        active->getResult().setObjectValue(createOwner(17));
    }, {}, owner, "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("makeOwnedPoint", [](VirtualMachine* active) {
        auto* destination = active->getParameter(1).getStorageAddress();
        destination[0] = active->getParameter(0);
        destination[1].freeObjectValue();
        destination[1].setStringValue("made");
    }, {defParameter("number", runtime.getIntegerType(), "")}, point, "").isValid());
    auto emptyValues = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction("makeOwnedEmptyValue", [&](VirtualMachine*) {
        ++emptyValues;
    }, {}, empty, "").isValid());
    ScriptObject* valueOwner = nullptr;
    std::vector<NomadInteger> assignmentOrder;
    BOOST_REQUIRE(runtime.registerNativeFunction("getValueOwner", [&](VirtualMachine* active) {
        assignmentOrder.push_back(0);
        valueOwner = createOwner(17);
        active->getResult().setObjectValue(valueOwner);
    }, {}, owner, "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("inspectValueOwner", [&](VirtualMachine* active) {
        assignmentOrder.push_back(1);
        BOOST_TEST(valueOwner->getReferenceCount() == 1U);
        BOOST_TEST(valueOwner->getStorageValue(2).getIntegerValue() == 17);
        BOOST_TEST(NomadString(valueOwner->getStorageValue(3).getStringValue()) == "kept");
        active->setIntegerResult(90);
    }, {}, runtime.getIntegerType(), "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("inspectThenMakePoint", [&](VirtualMachine* active) {
        assignmentOrder.push_back(2);
        BOOST_TEST(valueOwner->getReferenceCount() == 1U);
        BOOST_TEST(valueOwner->getStorageValue(2).getIntegerValue() == 17);
        BOOST_TEST(NomadString(valueOwner->getStorageValue(3).getStringValue()) == "kept");
        auto* destination = active->getParameter(0).getStorageAddress();
        destination[0].setIntegerValue(89);
        destination[1].freeObjectValue();
        destination[1].setStringValue("copied");
    }, {}, point, "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("makeOnlyEmptyOwner", [emptyOwner](VirtualMachine* active) {
        auto* object = new ScriptObject(emptyOwner);
        BOOST_TEST(object->getStorageSize() == 0U);
        active->getResult().setObjectValue(object);
    }, {}, emptyOwner, "").isValid());
    BOOST_REQUIRE(runtime.addMethod(pointId, {
        .name = "ownedShift",
        .parameters = {{runtime.getIntegerType()->getId(), "delta", ""}},
        .returnTypeId = runtime.getVoidType()->getId(),
        .function = [](VirtualMachine* active) {
            auto& number = active->getStorageParameter(1, 0);
            number.setIntegerValue(number.getIntegerValue() + active->getIntegerParameter(0));
        },
        .documentation = ""
    }).isValid());
    BOOST_REQUIRE(runtime.addMethod(pointId, {
        .name = "ownedRead",
        .parameters = {},
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [](VirtualMachine* active) {
            active->setIntegerResult(active->getStorageParameter(0, 0).getIntegerValue());
        },
        .documentation = ""
    }).isValid());
    BOOST_REQUIRE(runtime.addMethod(pointId, {
        .name = "ownedText",
        .parameters = {},
        .returnTypeId = runtime.getStringType()->getId(),
        .function = [](VirtualMachine* active) {
            active->getResult().copyObjectValue(active->getStorageParameter(0, 1));
        },
        .documentation = ""
    }).isValid());
    BOOST_REQUIRE(runtime.addMethod(pointId, {
        .name = "ownedCopy",
        .parameters = {},
        .returnTypeId = pointId,
        .function = [point](VirtualMachine* active) {
            auto* destination = active->getParameter(1).getStorageAddress();
            point->freeValue(destination);
            point->copyValue(&active->getStorageParameter(0, 0), destination);
        },
        .documentation = ""
    }).isValid());
    const auto pointMethod = runtime.registerScriptMethod(pointId, "ownedScript",
        {defParameter("delta", runtime.getIntegerType(), "")}, "ownedPoint.nomad",
        "this.number = this.number + delta\nthis.text = this.text\nreturn this");
    const auto outerMethod = runtime.registerScriptMethod(outerId, "ownedOuterScript",
        {defParameter("delta", runtime.getIntegerType(), "")}, "ownedOuter.nomad",
        "return this.point.ownedScript delta");
    const auto ownerMethod = runtime.registerScriptMethod(ownerId, "ownedOwnerShift",
        {defParameter("delta", runtime.getIntegerType(), "")}, "ownedReceiver.nomad",
        "this.outer.point.ownedShift delta\nreturn this.outer.point.ownedRead");
    BOOST_REQUIRE(pointMethod.isValid());
    BOOST_REQUIRE(outerMethod.isValid());
    BOOST_REQUIRE(ownerMethod.isValid());
    ScriptObject* originalVictim = nullptr;
    auto replacements = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction("replaceOwnedReceiver", [&](VirtualMachine* active) {
        const auto* main = runtime.getScriptFunction(runtime.getFunctionId("main"));
        auto& value = active->getLocalAddress(main->getVariableOffset(main->getVariableId("victim")))[0];
        originalVictim = static_cast<ScriptObject*>(value.getObjectValue());
        BOOST_TEST(originalVictim->getReferenceCount() == 2U);
        value.freeObjectValue();
        value.setObjectValue(createOwner(100));
        BOOST_TEST(originalVictim->getReferenceCount() == 1U);
        ++replacements;
        active->setIntegerResult(5);
    }, {}, runtime.getIntegerType(), "").isValid());
    BOOST_REQUIRE(runtime.addMethod(pointId, {
        .name = "ownedSwapRead",
        .parameters = {{runtime.getIntegerType()->getId(), "delta", ""}},
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [&](VirtualMachine* active) {
            BOOST_REQUIRE(active->peekStack(2).getObjectValue() == originalVictim);
            BOOST_TEST(originalVictim->getReferenceCount() == 1U);
            auto& number = active->getStorageParameter(1, 0);
            number.setIntegerValue(number.getIntegerValue() + active->getIntegerParameter(0));
            active->setIntegerResult(number.getIntegerValue());
        },
        .documentation = ""
    }).isValid());
    std::vector<NomadInteger> order;
    auto orderedCalls = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction("getOrderedOwner", [&](VirtualMachine* active) {
        ++orderedCalls;
        order.clear();
        order.push_back(0);
        active->getResult().setObjectValue(createOwner(17));
    }, {}, owner, "").isValid());
    for (const auto number : {1, 2}) {
        BOOST_REQUIRE(runtime.registerNativeFunction("ownedArgument" + std::to_string(number),
            [&, number](VirtualMachine* active) {
                order.push_back(number);
                active->setIntegerResult(number);
            }, {}, runtime.getIntegerType(), "").isValid());
    }
    BOOST_REQUIRE(runtime.addMethod(pointId, {
        .name = "ownedOrder",
        .parameters = {
            {runtime.getIntegerType()->getId(), "first", ""},
            {runtime.getIntegerType()->getId(), "second", ""}
        },
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [&](VirtualMachine* active) {
            order.push_back(3);
            active->setIntegerResult(active->getStorageParameter(2, 0).getIntegerValue() +
                active->getIntegerParameter(0) + active->getIntegerParameter(1));
        },
        .documentation = ""
    }).isValid());
    auto emptyCalls = 0;
    BOOST_REQUIRE(runtime.addMethod(emptyId, {
        .name = "ownedEmpty",
        .parameters = {},
        .returnTypeId = emptyId,
        .function = [&](VirtualMachine* active) {
            BOOST_REQUIRE(active->peekStack(0).getObjectValue() != nullptr);
            ++emptyCalls;
        },
        .documentation = ""
    }).isValid());
    BOOST_REQUIRE(runtime.addMethod(emptyId, {
        .name = "ownedCreate",
        .parameters = {},
        .returnTypeId = pointId,
        .function = [](VirtualMachine* active) {
            BOOST_REQUIRE(active->peekStack(1).getObjectValue() != nullptr);
            auto* destination = active->getParameter(0).getStorageAddress();
            destination[0].setIntegerValue(123);
            destination[1].freeObjectValue();
            destination[1].setStringValue("empty created");
        },
        .documentation = ""
    }).isValid());
    BOOST_REQUIRE(runtime.addMethod(emptyId, {
        .name = "ownedEmptyNumber",
        .parameters = {},
        .returnTypeId = runtime.getIntegerType()->getId(),
        .function = [](VirtualMachine* active) {
            BOOST_REQUIRE(active->peekStack(0).getObjectValue() != nullptr);
            active->setIntegerResult(77);
        },
        .documentation = ""
    }).isValid());
    fixture.addFunction("main",
        "class ExplicitOwnedBox\npoint = makeOwnedPoint 7\nempty = makeOwnedEmptyValue\n"
        "fun replaceExplicitOwnedPoint number:int\nthis.point = makeOwnedPoint number\nreturn this.point.number\nend\nend\n"
        "class InferredOwnedBox\ninit\nassert this.point.number == 0\nassert this.point.text == \"\"\n"
        "this.point = makeOwnedPoint 5\nend\nend\n"
        "fun takeOwnedPoint value:OwnedPoint\nvalue.number = value.number + 10\nreturn value.number\nend\n"
        "fun relayOwned owner:OwnedReceiver\nreturn owner.outer.point.ownedCopy\nend\n"
        "fun readOwned owner:OwnedReceiver\nreturn owner.outer.point\nend\n"
        "fun takeOwnedEmpty value:OwnedEmpty\nreturn 7\nend\n"
        "owner = makeOwnedReceiver\nowner.outer.point.ownedShift 3\n"
        "assert (owner.outer.point.ownedRead) == 20\n"
        "result = owner.outer.point.ownedScript 2\nresult.number = 500\n"
        "assert (owner.outer.point.ownedRead) == 22\n"
        "outerCopy = owner.outer.ownedOuterScript 1\nassert outerCopy.number == 23\n"
        "assert (owner.ownedOwnerShift 4) == 27\n"
        "assert (owner.outer.point.ownedText) == \"kept\"\n"
        "text = (makeOwnedReceiver).outer.point.ownedText\nassert text == \"kept\"\n"
        "copy = (makeOwnedReceiver).outer.point.ownedCopy\nassert copy.number == 17\nassert copy.text == \"kept\"\n"
        "owner.outer.point.ownedScript 0\n"
        "assert (takeOwnedPoint (owner.outer.point.ownedCopy)) == 37\n"
        "relayed = relayOwned owner\nassert relayed.number == 27\n"
        "victim = makeOwnedReceiver\nswapped = victim.outer.point.ownedScript replaceOwnedReceiver\n"
        "assert swapped.number == 22\nassert swapped.text == \"kept\"\n"
        "assert (victim.outer.point.ownedRead) == 100\n"
        "victim = makeOwnedReceiver\n"
        "assert (victim.outer.point.ownedSwapRead replaceOwnedReceiver) == 22\n"
        "assert (victim.outer.point.ownedRead) == 100\n"
        "assert ((getOrderedOwner).outer.point.ownedOrder ownedArgument1 ownedArgument2) == 20\n"
        "assert (onlyOwnedScalar) == 17\njustReceiver = onlyOwnedStruct\nassert justReceiver.number == 18\n"
        "owner.outer.point.ownedShift (owner.outer.point.ownedRead)\n"
        "assert owner.prefix == 101\nassert owner.suffix == 505\n"
        "assert (takeOwnedPoint owner.outer.point) == 64\n"
        "assert (takeOwnedPoint (makeOwnedReceiver).outer.point) == 27\n"
        "snapshot = owner.outer\nsnapshot.point.number = 700\nassert owner.outer.point.number == 54\n"
        "owner.outer.point.number = 61\nowner.outer.point.text = owner.outer.point.text\n"
        "owner.outer.point = owner.outer.point\nowner.outer = owner.outer\n"
        "assert owner.outer.before == 202\nassert owner.outer.after == 404\n"
        "pointCopy = readOwned owner\npointCopy.number = 900\nassert owner.outer.point.number == 61\n"
        "owner.outer.point = makeOwnedPoint 9\n"
        "owner.outer.point = owner.outer.point.ownedScript 2\nassert owner.outer.point.number == 11\n"
        "owner.outer.point = makeOwnedPoint (owner.outer.point.number + 1)\n"
        "assert owner.outer.point.number == 12\nassert owner.outer.point.text == \"made\"\n"
        "(getValueOwner).outer.point.number = inspectValueOwner\n"
        "(getValueOwner).outer.point = inspectThenMakePoint\n"
        "victim = makeOwnedReceiver\nvictim.outer.point.number = replaceOwnedReceiver\n"
        "assert victim.outer.point.number == 100\n"
        "victim = makeOwnedReceiver\nvictim.outer.point = makeOwnedPoint replaceOwnedReceiver\n"
        "assert victim.outer.point.number == 100\n"
        "emptyValue = (makeOnlyEmptyOwner).empty\nowner.empty = emptyValue\n"
        "(makeOnlyEmptyOwner).empty = makeOwnedEmptyValue\n"
        "owner.empty = (makeOnlyEmptyOwner).empty\n"
        "assert (takeOwnedEmpty (makeOnlyEmptyOwner).empty) == 7\n"
        "explicit = new ExplicitOwnedBox\nassert explicit.point.number == 7\n"
        "assert (explicit.replaceExplicitOwnedPoint 31) == 31\n"
        "inferred = new InferredOwnedBox\nassert inferred.point.number == 5\n"
        "return owner.outer.point.ownedRead");
    fixture.addFunction("onlyEmpty",
        "owner = makeOwnedReceiver\nowner.empty.ownedEmpty\n(makeOnlyEmptyOwner).empty.ownedEmpty\n"
        "created = (makeOnlyEmptyOwner).empty.ownedCreate\n"
        "assert created.number == 123\nassert created.text == \"empty created\"\n"
        "return (makeOwnedReceiver).empty.ownedEmptyNumber");
    fixture.addFunction("onlyOwnedScalar", "return (makeOwnedReceiver).outer.point.ownedRead");
    fixture.addFunction("onlyOwnedStruct", "return (makeOwnedReceiver).outer.ownedOuterScript 1");
    fixture.getCompiler().registerPhaseHook([&](CompilerContext* context, const CompilationPhase phase) {
        if (phase != CompilationPhase::Parse) {
            return;
        }
        for (const auto id : {pointMethod, outerMethod, ownerMethod}) {
            auto* method = runtime.getScriptCallable(id);
            fixture.getCompiler().registerMemberCompilation(
                id, runtime.getFunctionId("main"), CompilationCallableTarget::Function);
            auto body = std::make_unique<FunctionNode>(0, 0);
            Tokenizer tokens(context, method->getPath(), method->getSourceCode());
            while (tokens.nextLine()) {
                if (auto statement = parser::parseLine(context, method, &tokens)) {
                    body->addStatement(std::move(statement));
                }
            }
            fixture.getCompiler().setFunctionBody(id, std::move(body));
        }
    });
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    const std::vector<NomadInteger> expectedOrder{0, 2, 1, 3};
    for (auto index = 0; index < 3; ++index) {
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 12);
        BOOST_TEST(order == expectedOrder, boost::test_tools::per_element());
        const auto emptyOutcome = fixture.execute("onlyEmpty");
        BOOST_REQUIRE_MESSAGE(!emptyOutcome.fault.has_value(), emptyOutcome.fault.value_or(""));
        BOOST_TEST(emptyOutcome.integerValue == 77);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(replacements == 12);
    BOOST_TEST(emptyValues == 6);
    BOOST_TEST(orderedCalls == 3);
    BOOST_TEST(emptyCalls == 6);
    std::ostringstream dump;
    dumpInstructions(runtime, *runtime.getScriptFunction(runtime.getFunctionId("onlyEmpty")), dump);
    BOOST_TEST(dump.str().find("op_stored_property_address_push") == NomadString::npos);
    BOOST_TEST(dump.str().find("op_object_free_stack") != NomadString::npos);
    auto sawOwnerAtTop = false;
    auto sawOwnerBelowReturn = false;
    const auto addressOpcode = runtime.getInstructionId(OpCodes::op_stored_property_address_push);
    const auto& instructions = runtime.getInstructions();
    for (const auto* name : {"onlyOwnedScalar", "onlyOwnedStruct"}) {
        const auto* function = runtime.getScriptFunction(runtime.getFunctionId(name));
        for (auto position = function->getFunctionStart(); position < function->getFunctionEnd();) {
            const auto opcode = runtime.getInstructionId(instructions[position].fn);
            BOOST_REQUIRE(opcode.isValid());
            if (opcode == addressOpcode) {
                const auto ownerOffset = instructions[position + 1].value.getIndexValue();
                const auto firstOffset = instructions[position + 2].value.getIndexValue();
                const auto lastOffset = instructions[position + 3].value.getIndexValue();
                BOOST_TEST(ownerOffset <= 1U);
                BOOST_CHECK((firstOffset == 2 && lastOffset == 3) || (firstOffset == 1 && lastOffset == 4));
                sawOwnerAtTop = sawOwnerAtTop || ownerOffset == 0;
                sawOwnerBelowReturn = sawOwnerBelowReturn || ownerOffset == 1;
            }
            position += 1 + runtime.getInstructionOperands(opcode).size();
        }
    }
    BOOST_TEST(sawOwnerAtTop);
    BOOST_TEST(sawOwnerBelowReturn);
    const std::vector<NomadInteger> expectedAssignments{0, 1, 0, 2, 0, 1, 0, 2, 0, 1, 0, 2};
    BOOST_TEST(assignmentOrder == expectedAssignments, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(native_temporary_property_paths_preserve_receiver_storage_and_result_ownership) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto innerId = runtime.registerStruct("TemporaryInner");
    const auto outerId = runtime.registerStruct("TemporaryOuter");
    const auto* inner = runtime.getTypeByName("TemporaryInner");
    const auto* outer = runtime.getTypeByName("TemporaryOuter");
    BOOST_REQUIRE(runtime.registerProperty(innerId, "number", runtime.getIntegerType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(innerId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "inner", inner).isValid());
    BOOST_REQUIRE(runtime.registerProperty(outerId, "unrelated", runtime.getStringType()).isValid());
    runtime.finalizeCompositeFieldLayout(outerId);
    auto roots = 0;
    auto stringsBeforeRoot = RuntimeValue::getLiveStringCount();
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "produceTemporary",
        [&](VirtualMachine* active) {
            ++roots;
            stringsBeforeRoot = RuntimeValue::getLiveStringCount();
            auto* destination = active->getParameter(1).getStorageAddress();
            destination[0] = active->getParameter(0);
            destination[1].freeObjectValue();
            destination[1].setStringValue("inner");
            destination[2].freeObjectValue();
            destination[2].setStringValue("outer");
        },
        {defParameter("number", runtime.getIntegerType(), "")},
        outer,
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "temporaryArgument",
        [&](VirtualMachine* active) {
            BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBeforeRoot + 2);
            active->setIntegerResult(3);
        },
        {},
        runtime.getIntegerType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "temporaryReleased",
        [&](VirtualMachine*) {
            BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBeforeRoot);
        },
        {},
        runtime.getVoidType(),
        ""
    ).isValid());
    BOOST_REQUIRE(runtime.addMethod(innerId, {
        .name = "shifted",
        .parameters = {{runtime.getIntegerType()->getId(), "amount", ""}},
        .returnTypeId = innerId,
        .function = [inner](VirtualMachine* active) {
            auto* receiver = active->getParameter(1).getStorageAddress();
            auto* destination = active->getParameter(2).getStorageAddress();
            BOOST_TEST(receiver != destination);
            BOOST_TEST(receiver[1].getObjectValue()->getReferenceCount() == 1U);
            receiver[0].setIntegerValue(receiver[0].getIntegerValue() + active->getIntegerParameter(0));
            inner->freeValue(destination);
            inner->copyValue(receiver, destination);
            BOOST_TEST(receiver[1].getObjectValue()->getReferenceCount() == 2U);
        }
    }).isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "inspectTemporary",
        [](VirtualMachine* active) {
            const auto value = active->getParameterStorage(0, 2);
            BOOST_TEST(value[1].getObjectValue()->getReferenceCount() ==
                static_cast<NomadIndex>(active->getIntegerParameter(2)));
            BOOST_TEST(value[1].getStringObject()->getString() == "inner");
            active->setIntegerResult(value[0].getIntegerValue());
        },
        {defParameter("value", inner, ""), defParameter("references", runtime.getIntegerType(), "")},
        runtime.getIntegerType(),
        ""
    ).isValid());
    fixture.addFunction("main",
        "fun forwardTemporary number:int\nreturn (produceTemporary number).inner.shifted temporaryArgument\nend\n"
        "copy = forwardTemporary 7\n"
        "assert (inspectTemporary copy 2) == 10\n"
        "assert (produceTemporary 11).inner.number == 11\ntemporaryReleased\n"
        "assert (produceTemporary 12).inner.text == \"inner\"\ntemporaryReleased\n"
        "assert (inspectTemporary (produceTemporary 13).inner 1) == 13\ntemporaryReleased\n"
        "(produceTemporary 14).inner.number = temporaryArgument\ntemporaryReleased\n"
        "return copy.number");
    const auto compiled = fixture.compile();
    BOOST_REQUIRE_MESSAGE(compiled, fixture.getDiagnostics());
    const auto* forward = runtime.getScriptFunction(runtime.getFunctionId("forwardTemporary"));
    BOOST_REQUIRE(forward != nullptr);
    BOOST_TEST(forward->getVariableCount() == 1U);
    BOOST_TEST(forward->getVariableStorageSize() == 3U);
    BOOST_TEST(forward->getVariableName(VariableId{0}).starts_with("$propertyReceiver"));
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        roots = 0;
        const auto outcome = fixture.execute("main");
        BOOST_REQUIRE_MESSAGE(!outcome.fault.has_value(), outcome.fault.value_or(""));
        BOOST_TEST(outcome.integerValue == 10);
        BOOST_TEST(roots == 5);
        BOOST_TEST(outcome.leakedStrings == 0);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
}

BOOST_AUTO_TEST_CASE(source_struct_properties_reject_unknown_properties_and_type_mismatches) {
    const std::array<std::pair<NomadString, NomadString>, 7> cases{{
        {"value = makeBoundaryLeaf\nreturn value.missing", "Unknown struct property"},
        {"value = makeBoundaryLeaf\nvalue.missing = 3", "Unknown struct property"},
        {"value = makeBoundaryLeaf\nvalue.number = \"bad\"", "Cannot assign value of type"},
        {"return (makeBoundaryLeaf).missing", "Unknown struct property"},
        {"owner = makeBoundaryOwner\nowner.value.number = \"bad\"", "Cannot assign value of type"},
        {"owner = makeBoundaryOwner\nowner.value = 3",
            "Cannot assign type 'int' to class field 'value' of type 'BoundaryLeaf'"},
        {"owner = makeBoundaryOwner\nreturn owner.value.missing", "Unknown struct property"}
    }};
    for (const auto& [source, diagnostic] : cases) {
        nomad::test::LanguageTestFixture fixture;
        auto& runtime = fixture.getRuntime();
        const auto leafId = runtime.registerStruct("BoundaryLeaf");
        const auto ownerId = runtime.registerClass("BoundaryOwner", ClassMode::Closed);
        const auto* leaf = runtime.getTypeByName("BoundaryLeaf");
        const auto* owner = runtime.getTypeByName("BoundaryOwner");
        BOOST_REQUIRE(runtime.registerProperty(leafId, "number", runtime.getIntegerType()).isValid());
        BOOST_REQUIRE(runtime.registerProperty(ownerId, "value", leaf).isValid());
        runtime.finalizeCompositeFieldLayout(ownerId);
        auto calls = 0;
        BOOST_REQUIRE(runtime.registerNativeFunction("makeBoundaryLeaf", [&](VirtualMachine*) {
            ++calls;
        }, {}, leaf, "").isValid());
        BOOST_REQUIRE(runtime.registerNativeFunction("makeBoundaryOwner", [&](VirtualMachine*) {
            ++calls;
        }, {}, owner, "").isValid());
        fixture.addFunction("main", source);
        BOOST_TEST(!fixture.compile());
        BOOST_CHECK_MESSAGE(fixture.hasError(diagnostic), fixture.getDiagnostics());
        BOOST_TEST(calls == 0);
    }
}

BOOST_AUTO_TEST_CASE(registered_native_struct_constructors_create_nonempty_and_empty_values) {
    for (const auto empty : {false, true}) {
        nomad::test::LanguageTestFixture fixture;
        auto& runtime = fixture.getRuntime();
        const auto id = runtime.registerStruct("PendingConstruction");
        if (!empty) {
            BOOST_REQUIRE(runtime.registerProperty(id, "number", runtime.getIntegerType()).isValid());
        }
        runtime.finalizeCompositeFieldLayout(id);
        auto calls = 0;
        BOOST_REQUIRE(runtime.registerNativeStructConstructor(id, {
            .parameters = {},
            .function = [&](VirtualMachine*) { ++calls; },
            .documentation = ""
        }).isValid());
        fixture.addFunction("main", "value = new PendingConstruction");
        BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
        RuntimeValue result;
        runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
        BOOST_TEST(calls == 1);
    }
}

BOOST_AUTO_TEST_CASE(registered_empty_script_struct_constructor_bodies_create_values_without_object_initializers) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto id = runtime.registerStruct("PendingBody");
    runtime.finalizeCompositeFieldLayout(id);
    const auto constructorId = runtime.registerScriptStructConstructor(id, {}, "pending.nomad", "");
    BOOST_REQUIRE(constructorId.isValid());
    fixture.addFunction("main", "value = new PendingBody");
    fixture.getCompiler().registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
        if (phase != CompilationPhase::Parse) {
            return;
        }
        fixture.getCompiler().registerMemberCompilation(
            constructorId, runtime.getFunctionId("main"), CompilationCallableTarget::Constructor);
        fixture.getCompiler().setFunctionBody(constructorId, std::make_unique<FunctionNode>(0, 0));
    });
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
}

BOOST_AUTO_TEST_CASE(struct_creation_initializes_properties_before_bodies_and_preserves_copy_ownership) {
    nomad::test::LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto nativeId = runtime.registerStruct("NativeCreated");
    const auto scriptId = runtime.registerStruct("ScriptCreated");
    const auto emptyId = runtime.registerStruct("EmptyCreated");
    const auto* nativeType = runtime.getTypeByName("NativeCreated");
    const auto* scriptType = runtime.getTypeByName("ScriptCreated");
    const auto* emptyType = runtime.getTypeByName("EmptyCreated");
    const auto nativeNumber = runtime.registerScriptStructProperty(nativeId, "number", runtime.getIntegerType());
    BOOST_REQUIRE(nativeNumber.isValid());
    BOOST_REQUIRE(runtime.registerScriptStructProperty(nativeId, "text", runtime.getStringType()).isValid());
    const auto scriptNumber = runtime.registerScriptStructProperty(scriptId, "number", runtime.getIntegerType());
    BOOST_REQUIRE(scriptNumber.isValid());
    BOOST_REQUIRE(runtime.registerScriptStructProperty(scriptId, "text", runtime.getStringType()).isValid());
    BOOST_REQUIRE(runtime.registerScriptStructProperty(scriptId, "nested", nativeType).isValid());
    runtime.setPropertyHasExplicitInitializer(nativeNumber, true);
    runtime.setPropertyHasExplicitInitializer(scriptNumber, true);
    runtime.finalizeCompositeFieldLayout(scriptId);
    runtime.finalizeCompositeFieldLayout(emptyId);
    auto initializers = 0;
    auto nativeCalls = 0;
    auto emptyCalls = 0;
    BOOST_REQUIRE(runtime.registerNativeFunction("initialNumber", [&](VirtualMachine* active) {
        ++initializers;
        active->setIntegerResult(42);
    }, {}, runtime.getIntegerType(), "").isValid());
    std::vector<NomadInteger> argumentOrder;
    BOOST_REQUIRE(runtime.registerNativeFunction("argumentNumber", [&](VirtualMachine* active) {
        argumentOrder.push_back(1);
        active->setIntegerResult(7);
    }, {}, runtime.getIntegerType(), "").isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("argumentText", [&](VirtualMachine* active) {
        argumentOrder.push_back(2);
        active->setStringResult("owned");
    }, {}, runtime.getStringType(), "").isValid());
    const auto nativeConstructor = runtime.registerNativeStructConstructor(nativeId, {
        .parameters = {
            {runtime.getIntegerType()->getId(), "number", ""},
            {runtime.getStringType()->getId(), "text", ""}
        },
        .function = [&](VirtualMachine* active) {
            BOOST_TEST(active->getStorageParameter(2, 0).getIntegerValue() == 42);
            BOOST_TEST(active->getStorageParameter(2, 1).getStringObject()->getString().empty());
            active->getStorageParameter(2, 0) = active->getParameter(0);
            active->getStorageParameter(2, 1).freeObjectValue();
            active->getStorageParameter(2, 1).copyObjectValue(active->getParameter(1));
            ++nativeCalls;
        }
    });
    BOOST_REQUIRE(nativeConstructor.isValid());
    const auto scriptConstructor = runtime.registerScriptStructConstructor(scriptId,
        {defParameter("copy", nativeType, "")}, "created.nomad",
        "assert this.number == 42\nassert this.text == \"\"\n"
        "assert this.nested.number == 0\nassert this.nested.text == \"\"\n"
        "this.nested = copy\nthis.text = copy.text\nthis.number = copy.number\ncopy.number = 99");
    BOOST_REQUIRE(scriptConstructor.isValid());
    const auto emptyConstructor = runtime.registerScriptStructConstructor(emptyId, {}, "empty.nomad", "recordEmpty");
    BOOST_REQUIRE(emptyConstructor.isValid());
    BOOST_REQUIRE(runtime.registerNativeFunction("recordEmpty", [&](VirtualMachine*) {
        ++emptyCalls;
    }, {}, runtime.getVoidType(), "").isValid());
    const auto nativeInitializer = runtime.registerScriptStructMethod(nativeId,
        nativeType->asCompositeType()->getInitializerName(), {}, "", "");
    const auto scriptInitializer = runtime.registerScriptStructMethod(scriptId,
        scriptType->asCompositeType()->getInitializerName(), {}, "", "");
    BOOST_REQUIRE(nativeInitializer.isValid());
    BOOST_REQUIRE(scriptInitializer.isValid());
    auto nativeDeclaration = std::make_unique<StructMemberDeclaration>(
        0, 0, StructMemberKind::Field, "number", std::vector<Parameter>{}, std::vector<StructMemberSourceLine>{});
    nativeDeclaration->setPropertyId(nativeNumber);
    nativeDeclaration->setInitializer(std::make_unique<FunctionCallExpression>(nullptr, 0, 0, "initialNumber"));
    auto scriptDeclaration = std::make_unique<StructMemberDeclaration>(
        0, 0, StructMemberKind::Field, "number", std::vector<Parameter>{}, std::vector<StructMemberSourceLine>{});
    scriptDeclaration->setPropertyId(scriptNumber);
    scriptDeclaration->setInitializer(std::make_unique<FunctionCallExpression>(nullptr, 0, 0, "initialNumber"));
    fixture.addFunction("createCopy",
        "return new ScriptCreated (new NativeCreated argumentNumber argumentText)");
    fixture.addFunction("main",
        "fun acceptCopy value:ScriptCreated\nvalue.number = 111\nreturn value.nested.number\nend\n"
        "native = new NativeCreated argumentNumber argumentText\n"
        "script = new ScriptCreated native\n"
        "assert native.number == 7\nassert native.text == \"owned\"\n"
        "assert script.number == 7\nassert script.nested.number == 7\n"
        "native.number = 8\nassert script.nested.number == 7\n"
        "copied = createCopy\nassert copied.text == \"owned\"\n"
        "assert (acceptCopy (new ScriptCreated native)) == 8\n"
        "assert script.number == 7\n"
        "class CreatedOwner\nvalue = new ScriptCreated (new NativeCreated 5 \"property\")\nend\n"
        "owner = new CreatedOwner\nassert owner.value.nested.number == 5\n"
        "owner.value = new ScriptCreated (new NativeCreated 6 \"replaced\")\n"
        "assert owner.value.nested.number == 6\nassert owner.value.text == \"replaced\"\n"
        "empty = new EmptyCreated\nempty = new EmptyCreated\n"
        "return 1");
    fixture.getCompiler().registerPhaseHook([&](CompilerContext* context, const CompilationPhase phase) {
        if (phase != CompilationPhase::Parse) {
            return;
        }
        for (const auto id : {nativeInitializer, scriptInitializer}) {
            runtime.getScriptCallable(id)->setReturnType(runtime.getVoidType());
            fixture.getCompiler().registerMemberCompilation(
                id, runtime.getFunctionId("main"), CompilationCallableTarget::PropertyInitializer);
            auto body = std::make_unique<FunctionNode>(0, 0);
            body->addStatement(std::make_unique<StructFieldInitializerStatement>(
                id == nativeInitializer ? nativeDeclaration.get() : scriptDeclaration.get()));
            fixture.getCompiler().setFunctionBody(id, std::move(body));
        }
        for (const auto id : {scriptConstructor, emptyConstructor}) {
            auto* constructor = runtime.getScriptCallable(id);
            fixture.getCompiler().registerMemberCompilation(
                id, runtime.getFunctionId("main"), CompilationCallableTarget::Constructor);
            auto body = std::make_unique<FunctionNode>(0, 0);
            Tokenizer tokens(context, constructor->getPath(), constructor->getSourceCode());
            while (tokens.nextLine()) {
                if (auto statement = parser::parseLine(context, constructor, &tokens)) {
                    body->addStatement(std::move(statement));
                }
            }
            fixture.getCompiler().setFunctionBody(id, std::move(body));
        }
    });
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    for (auto index = 0; index < 3; ++index) {
        RuntimeValue result;
        runtime.executeFunction(runtime.getFunctionId("main"), {}, result);
        BOOST_TEST(result.getIntegerValue() == 1);
        BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
        BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore);
    }
    BOOST_TEST(nativeCalls == 12);
    BOOST_TEST(initializers == 27);
    BOOST_TEST(emptyCalls == 6);
    BOOST_REQUIRE(argumentOrder.size() == 12U);
    for (auto index = NomadIndex{0}; index < argumentOrder.size(); index += 2) {
        BOOST_TEST(argumentOrder[index] == 2);
        BOOST_TEST(argumentOrder[index + 1] == 1);
    }
    auto machine = runtime.createVirtualMachine();
    std::array<RuntimeValue, 2> nativeStorage;
    RuntimeValue text;
    text.setStringValue("host");
    machine->pushStorageAddress(nativeStorage.data());
    machine->pushObjectValue(text);
    machine->pushInteger(23);
    machine->setIntegerResult(41);
    machine->setIntegerIntermediate(43);
    machine->createNativeStruct(nativeStorage.data(), nativeConstructor);
    BOOST_TEST(machine->getIntegerResult() == 41);
    BOOST_TEST(machine->getIntegerIntermediate() == 43);
    BOOST_TEST(nativeStorage[0].getIntegerValue() == 23);
    BOOST_TEST(nativeStorage[1].getStringObject()->getString() == "host");
    BOOST_TEST(machine->peekStack(0).getIntegerValue() == 23);
    machine->pop1();
    auto argument = machine->popValue();
    runtime.getStringType()->freeValue(&argument);
    machine->pop1();
    std::array<RuntimeValue, 4> scriptStorage;
    machine->pushStorageAddress(scriptStorage.data());
    machine->pushValue(nativeStorage[0]);
    machine->pushObjectValue(nativeStorage[1]);
    machine->createScriptStruct(scriptStorage.data(), scriptConstructor);
    BOOST_TEST(scriptStorage[0].getIntegerValue() == 23);
    BOOST_TEST(scriptStorage[2].getIntegerValue() == 23);
    BOOST_TEST(machine->getIntegerResult() == 41);
    BOOST_TEST(machine->getIntegerIntermediate() == 43);
    nativeType->freeValue(machine->getStackAddress(1));
    machine->pop2();
    machine->pop1();
    nativeType->freeValue(nativeStorage.data());
    scriptType->freeValue(scriptStorage.data());
    text.freeObjectValue();
    BOOST_TEST(emptyType->getSize() == 0U);
}

BOOST_AUTO_TEST_CASE(unsupported_emission_operations_reject_scalar_fallback_without_writing_instructions) {
    Runtime runtime;
    const auto id = runtime.registerStruct("Empty");
    const Type* type = runtime.getTypeByName("Empty");
    runtime.finalizeCompositeFieldLayout(id);
    std::vector<Instruction> instructions;
    Compiler compiler(&runtime, instructions);
    const auto initialSize = instructions.size();
    BOOST_CHECK_THROW(type->emitInitResult(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitInitIntermediate(compiler), NomadException);
    type->emitInitFunctionVariable(compiler, 0);
    BOOST_CHECK_THROW(type->emitFreeResult(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitFreeIntermediate(compiler), NomadException);
    type->emitFreeStackValue(compiler, 0);
    type->emitFreeFunctionVariable(compiler, 0);
    type->emitFreeParameter(compiler, 0);
    for (const auto target : {ExpressionTarget::Result, ExpressionTarget::Intermediate, ExpressionTarget::Stack}) {
        if (target == ExpressionTarget::Stack) {
            BOOST_TEST(type->emitLoadFunctionVariable(compiler, 0, target) == NOMAD_INVALID_INDEX);
            BOOST_TEST(type->emitLoadParameter(compiler, 0, target) == NOMAD_INVALID_INDEX);
        } else {
            BOOST_CHECK_THROW(type->emitLoadFunctionVariable(compiler, 0, target), NomadException);
            BOOST_CHECK_THROW(type->emitLoadParameter(compiler, 0, target), NomadException);
        }
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
