// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

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
    BOOST_CHECK_THROW(outer->emitFreeFunctionVariable(compiler, 0), NomadException);
    BOOST_TEST(instructions.size() == initialSize);
    runtime.finalizeCompositeFieldLayout(outerId);
    runtime.finalizeCompositeFieldLayout(scalarId);
    const auto offset = toNomadIndex(NOMAD_ID_MAX) + 1;
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
}

BOOST_AUTO_TEST_CASE(function_return_releases_nested_struct_locals_and_preserves_scalar_neighbors) {
    const auto before = RuntimeValue::getLiveStringCount();
    Runtime runtime;
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
    function->registerVariable("untouched", outer);
    const auto trailing = function->registerVariable("trailing", runtime.getIntegerType());
    const auto initialize = runtime.registerNativeFunction("initializeCleanupLocals",
        [&](VirtualMachine* machine) {
            machine->setIntegerVariableValue(leading, 17);
            machine->setIntegerVariableValue(trailing, 29);
            const auto storage = machine->getLocalStorage(function->getVariableOffset(value), outer->getSize());
            storage[0].setIntegerValue(31);
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
    }
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
    BOOST_CHECK_THROW(type->emitFreeResult(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitFreeIntermediate(compiler), NomadException);
    BOOST_CHECK_THROW(type->emitFreeStackValue(compiler, 0), NomadException);
    type->emitFreeFunctionVariable(compiler, 0);
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
