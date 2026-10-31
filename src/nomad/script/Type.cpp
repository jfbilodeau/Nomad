// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Type.hpp>

#include <algorithm>
#include <string>
#include <utility>

namespace nomad {

Type::Type(Runtime* runtime, const TypeId id, NomadString documentation):
    m_runtime(runtime),
    m_id(id),
    m_documentation(std::move(documentation))
{
    if (runtime == nullptr) {
        throw NomadException("A type requires an owning runtime");
    }
}

Runtime* Type::getRuntime() const {
    return m_runtime;
}

TypeId Type::getId() const {
    return m_id;
}

const NomadString& Type::getDocumentation() const {
    return m_documentation;
}

void Type::freeValue(RuntimeValue* /*value*/) const {
    // Default implementation does nothing
}

NomadIndex Type::getAssignmentScratchSize() const {
    const auto size = getSize();
    return size > 1 ? size : 0;
}

void Type::assignValue(
    const RuntimeValue* sourceValue,
    RuntimeValue* destinationValue,
    RuntimeValue* scratch
) const {
    const auto size = getSize();
    if (size == 0) {
        return;
    }
    RuntimeValue localScratch;
    if (scratch == nullptr) {
        if (size > 1) {
            throw NomadException("Assignment of type '" + getTypeName() + "' requires " +
                std::to_string(size) + " scratch slots");
        }
        scratch = &localScratch;
    }
    try {
        copyValue(sourceValue, scratch);
        freeValue(destinationValue);
    } catch (...) {
        freeValue(scratch);
        std::fill_n(scratch, size, RuntimeValue{});
        throw;
    }
    std::copy_n(scratch, size, destinationValue);
    std::fill_n(scratch, size, RuntimeValue{});
}

bool Type::needsFree() const {
    return false;
}

void Type::emitInitResult(OpCodeEmitter&) const {
}

void Type::emitInitIntermediate(OpCodeEmitter&) const {
}

void Type::emitFreeResult(OpCodeEmitter&) const {
}

void Type::emitFreeIntermediate(OpCodeEmitter&) const {
}

void Type::emitFreeStackValue(OpCodeEmitter&, NomadIndex) const {
}

void Type::emitFreeFunctionVariable(OpCodeEmitter&, NomadIndex) const {
}

NomadIndex Type::emitLoadFunctionVariable(
    OpCodeEmitter& emitter,
    const NomadId variableId,
    const ExpressionTarget target
) const {
    return emitter.addLoadTarget(
        op_function_variable_get_r,
        op_function_variable_get_i,
        op_function_variable_push,
        variableId,
        target
    );
}

NomadIndex Type::emitLoadParameter(
    OpCodeEmitter& emitter,
    const NomadId parameterId,
    const ExpressionTarget target
) const {
    return emitter.addLoadTarget(
        op_parameter_load_r,
        op_parameter_load_i,
        op_parameter_push,
        parameterId,
        target
    );
}

NomadIndex Type::emitLoadDynamicVariable(
    OpCodeEmitter& emitter,
    const NomadId variableId,
    const ExpressionTarget target
) const {
    return emitter.addLoadTarget(
        op_dynamic_variable_get_r,
        op_dynamic_variable_get_i,
        op_dynamic_variable_push,
        variableId,
        target
    );
}

NomadIndex Type::emitLoadContextVariable(
    OpCodeEmitter& emitter,
    const NomadId variableId,
    const ExpressionTarget target
) const {
    return emitter.addLoadTarget(
        op_context_variable_get_r,
        op_context_variable_get_i,
        op_context_variable_push,
        variableId,
        target
    );
}

NomadIndex Type::emitCopyResultToStack(OpCodeEmitter& emitter) const {
    return emitter.addOpCode(op_push_r);
}

void Type::emitMoveResultToIntermediate(OpCodeEmitter& emitter) const {
    emitter.addOpCode(op_copy_r_to_i);
}

void Type::emitMoveIntermediateToResult(OpCodeEmitter& emitter) const {
    emitter.addOpCode(op_copy_i_to_r);
}

void Type::emitMoveResultToStack(OpCodeEmitter& emitter) const {
    emitter.addOpCode(op_push_r);
}

NomadIndex Type::emitPushIntermediate(OpCodeEmitter& emitter) const {
    return emitter.addOpCode(op_push_i);
}

NomadIndex Type::emitPopResult(OpCodeEmitter& emitter) const {
    return emitter.addOpCode(op_pop_r);
}

NomadIndex Type::emitPopIntermediate(OpCodeEmitter& emitter) const {
    return emitter.addOpCode(op_pop_i);
}

NomadIndex Type::emitStoreParameter(OpCodeEmitter& emitter, const NomadId parameterId) const {
    emitter.addOpCode(op_parameter_set);
    return emitter.addId(parameterId);
}

NomadIndex Type::emitStoreFunctionVariable(
    OpCodeEmitter& emitter,
    const NomadId variableId
) const {
    emitter.addOpCode(op_function_variable_set);
    return emitter.addId(variableId);
}

NomadIndex Type::emitStoreDynamicVariable(
    OpCodeEmitter& emitter,
    const NomadId variableId
) const {
    emitter.addOpCode(op_dynamic_variable_set);
    return emitter.addId(variableId);
}

NomadIndex Type::emitStoreContextVariable(
    OpCodeEmitter& emitter,
    const NomadId variableId
) const {
    emitter.addOpCode(op_context_variable_set);
    return emitter.addId(variableId);
}

bool Type::acceptsArgument(const Type* argumentType) const {
    return sameType(argumentType);
}

bool Type::isInternal() const {
    return getTypeName().starts_with(FUNCTION_INTERNAL_NAME_PREFIX);
}

NomadString Type::toString(const RuntimeValue& value) const {
    NomadString text_value;

    toString(value, text_value);

    return text_value;
}

bool Type::sameType(const Type* other) const {
    if (other == nullptr) {
        return false;
    }

    if (other == this) {
        return true;
    }

    if (other->getTypeName() == getTypeName()) {
        return true;
    }

    return false;
}

const FunctionType* Type::asCallback() const {
    return nullptr;
}

const EventType * Type::asEvent() const {
    return nullptr;
}

const EnumType* Type::asEnum() const {
    return nullptr;
}

const ClassType* Type::asClassType() const {
    return nullptr;
}

const CompositeType* Type::asCompositeType() const {
    return nullptr;
}

bool Type::isVoid() const {
    return false;
}

bool Type::isString() const {
    return false;
}

NomadString getTypeDisplayName(const Type* type) {
    if (type == nullptr) {
        return "<unknown>";
    }

    const auto prefix = type->asEnum() == nullptr ? NomadString{} : NomadString{"enum "};
    return prefix + type->getTypeName();
}

} // namespace nomad
