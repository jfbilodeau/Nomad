// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerException.hpp>
#include <nomad/compiler/Expression.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/ScriptObject.hpp>
#include <nomad/script/StructType.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
#include <limits>
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

BOOST_AUTO_TEST_CASE(local_expression_destinations_handle_empty_structs_and_reject_unsupported_sources) {
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
    FunctionCallExpression unsupported(nullptr, 1, 1, "getEmptyExpression");
    IntegerLiteral unresolved(nullptr, 1, 1, 3);
    BOOST_REQUIRE(expression.resolve(&context, &function));
    BOOST_REQUIRE(parameter.resolve(&context, &function));
    BOOST_REQUIRE(unsupported.resolve(&context, &function));
    const auto initialSize = instructions.size();
    expression.compileIntoLocal(&compiler, &function, destination);
    expression.compileIntoLocal(&compiler, &function, source);
    BOOST_TEST(instructions.size() == initialSize);
    BOOST_CHECK_THROW(parameter.compileIntoLocal(&compiler, &function, destination), CompilerException);
    BOOST_CHECK_THROW(unsupported.compileIntoLocal(&compiler, &function, destination), CompilerException);
    BOOST_CHECK_THROW(expression.compileIntoLocal(&compiler, &function, wrongType), NomadBug);
    BOOST_CHECK_THROW(unresolved.compileIntoLocal(&compiler, &function, wrongType), NomadBug);
    BOOST_TEST(instructions.size() == initialSize);
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
