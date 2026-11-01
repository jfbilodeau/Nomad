// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/StructType.hpp>

#include <utility>

namespace nomad {

StructType::StructType(
    Runtime* runtime,
    TypeId id,
    NomadString name,
    NomadString documentation
):
    CompositeType(runtime, id, std::move(name), ClassMode::Closed, std::move(documentation))
{}

TypeStorage StructType::getStorage() const {
    return TypeStorage::Struct;
}

NomadIndex StructType::getSize() const {
    if (!isFieldLayoutFinalized()) {
        throw NomadException("Field layout of struct '" + getTypeName() + "' is not finalized");
    }
    return getFieldStorageSize();
}

bool StructType::sameType(const Type* other) const {
    return this == other;
}

const StructType* StructType::asStructType() const {
    return this;
}

bool StructType::needsFree() const {
    if (getSize() == 0) {
        return false;
    }
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() == nullptr && property->getValueType()->needsFree()) {
            return true;
        }
    }
    return false;
}

void StructType::initValue(RuntimeValue* value) const {
    if (getSize() == 0) {
        return;
    }
    auto offset = NomadIndex{0};
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        const auto* type = property->getValueType();
        const auto size = type->getSize();
        if (size != 0) {
            type->initValue(value + offset);
        }
        offset += size;
    }
}

void StructType::copyValue(const RuntimeValue* source, RuntimeValue* destination) const {
    if (getSize() == 0 || source == destination) {
        return;
    }
    auto offset = NomadIndex{0};
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        const auto* type = property->getValueType();
        const auto size = type->getSize();
        if (size != 0) {
            type->copyValue(source + offset, destination + offset);
        }
        offset += size;
    }
}

void StructType::freeValue(RuntimeValue* value) const {
    if (getSize() == 0) {
        return;
    }
    auto offset = NomadIndex{0};
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        const auto* type = property->getValueType();
        const auto size = type->getSize();
        if (size != 0) {
            type->freeValue(value + offset);
        }
        offset += size;
    }
}

void StructType::toString(const RuntimeValue&, NomadString& string) const {
    string = getTypeName();
}

void StructType::emitInitResult(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

void StructType::emitInitIntermediate(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

void StructType::emitInitFunctionVariable(
    OpCodeEmitter& emitter,
    const NomadIndex offset
) const {
    if (getSize() == 0) {
        return;
    }
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        property->getValueType()->emitInitFunctionVariable(
            emitter, offset + getFieldOffset(property->getId()));
    }
}

void StructType::emitFreeResult(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

void StructType::emitCopyFunctionVariable(
    OpCodeEmitter& emitter,
    const NomadIndex sourceOffset,
    const NomadIndex destinationOffset
) const {
    if (getSize() == 0 || sourceOffset == destinationOffset) {
        return;
    }
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        const auto fieldOffset = getFieldOffset(property->getId());
        property->getValueType()->emitCopyFunctionVariable(
            emitter, sourceOffset + fieldOffset, destinationOffset + fieldOffset);
    }
}

void StructType::emitFreeIntermediate(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

void StructType::emitFreeStackValue(OpCodeEmitter& emitter, const NomadIndex offset) const {
    const auto size = getSize();
    if (size == 0) {
        return;
    }
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        const auto* type = property->getValueType();
        if (type->needsFree()) {
            type->emitFreeStackValue(
                emitter, offset + (size - getFieldOffset(property->getId()) - type->getSize()));
        }
    }
}

void StructType::emitPushFunctionVariable(OpCodeEmitter& emitter, const NomadIndex offset) const {
    if (getSize() == 0) {
        return;
    }
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        property->getValueType()->emitPushFunctionVariable(emitter, offset + getFieldOffset(property->getId()));
    }
}

void StructType::emitFreeFunctionVariable(
    OpCodeEmitter& emitter,
    const NomadIndex offset
) const {
    if (getSize() == 0) {
        return;
    }
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        const auto* type = property->getValueType();
        if (type->needsFree()) {
            type->emitFreeFunctionVariable(emitter, offset + getFieldOffset(property->getId()));
        }
    }
}

void StructType::emitFreeParameter(
    OpCodeEmitter& emitter,
    const NomadIndex offset
) const {
    const auto size = getSize();
    if (size == 0) {
        return;
    }
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        const auto* type = property->getValueType();
        if (type->needsFree()) {
            const auto fieldOffset = getFieldOffset(property->getId());
            type->emitFreeParameter(emitter, offset + (size - fieldOffset - type->getSize()));
        }
    }
}

void StructType::emitCopyParameterToFunctionVariable(
    OpCodeEmitter& emitter,
    const NomadIndex parameterOffset,
    const NomadIndex destinationOffset
) const {
    const auto size = getSize();
    if (size == 0) {
        return;
    }
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        const auto* type = property->getValueType();
        const auto fieldOffset = getFieldOffset(property->getId());
        type->emitCopyParameterToFunctionVariable(
            emitter,
            parameterOffset + (size - fieldOffset - type->getSize()),
            destinationOffset + fieldOffset
        );
    }
}

NomadIndex StructType::emitLoadFunctionVariable(
    OpCodeEmitter& emitter,
    const NomadIndex offset,
    const ExpressionTarget target
) const {
    if (target != ExpressionTarget::Stack) {
        throw NomadException("Struct register load is not supported: " + getTypeName());
    }
    if (getSize() == 0) {
        return NOMAD_INVALID_INDEX;
    }
    auto firstOperand = NOMAD_INVALID_INDEX;
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr || property->getValueType()->getSize() == 0) {
            continue;
        }
        const auto operand = property->getValueType()->emitLoadFunctionVariable(
            emitter, offset + getFieldOffset(property->getId()), target);
        if (firstOperand == NOMAD_INVALID_INDEX) {
            firstOperand = operand;
        }
    }
    return firstOperand;
}

void StructType::emitCopyFunctionVariableToParameter(
    OpCodeEmitter& emitter,
    const NomadIndex sourceOffset,
    const NomadIndex parameterOffset
) const {
    const auto size = getSize();
    if (size == 0) {
        return;
    }
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        const auto* type = property->getValueType();
        const auto fieldOffset = getFieldOffset(property->getId());
        type->emitCopyFunctionVariableToParameter(
            emitter,
            sourceOffset + fieldOffset,
            parameterOffset + (size - fieldOffset - type->getSize())
        );
    }
}

NomadIndex StructType::emitLoadParameter(
    OpCodeEmitter& emitter,
    const NomadIndex offset,
    const ExpressionTarget target
) const {
    if (target != ExpressionTarget::Stack) {
        throw NomadException("Struct register load is not supported: " + getTypeName());
    }
    const auto size = getSize();
    auto firstOperand = NOMAD_INVALID_INDEX;
    for (const auto* property : getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        const auto* type = property->getValueType();
        if (type->getSize() == 0) {
            continue;
        }
        const auto operand = type->emitLoadParameter(
            emitter, offset + (size - getFieldOffset(property->getId()) - type->getSize()), target);
        if (firstOperand == NOMAD_INVALID_INDEX) {
            firstOperand = operand;
        }
    }
    return firstOperand;
}

NomadIndex StructType::emitLoadDynamicVariable(
    OpCodeEmitter&,
    NomadId,
    ExpressionTarget
) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitLoadContextVariable(
    OpCodeEmitter&,
    NomadId,
    ExpressionTarget
) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitCopyResultToStack(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

void StructType::emitMoveResultToIntermediate(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

void StructType::emitMoveIntermediateToResult(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

void StructType::emitMoveResultToStack(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitPushIntermediate(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitPopResult(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitPopIntermediate(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitStoreParameter(OpCodeEmitter&, NomadIndex) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitStoreFunctionVariable(OpCodeEmitter&, NomadIndex) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitStoreDynamicVariable(OpCodeEmitter&, NomadId) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitStoreContextVariable(OpCodeEmitter&, NomadId) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

} // namespace nomad
