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

void StructType::emitFreeResult(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

void StructType::emitFreeIntermediate(OpCodeEmitter&) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

void StructType::emitFreeStackValue(OpCodeEmitter&, NomadIndex) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

void StructType::emitFreeFunctionVariable(OpCodeEmitter&, NomadIndex) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

void StructType::emitFreeParameter(OpCodeEmitter&, NomadId) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitLoadFunctionVariable(
    OpCodeEmitter&,
    NomadId,
    ExpressionTarget
) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitLoadParameter(
    OpCodeEmitter&,
    NomadId,
    ExpressionTarget
) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
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

NomadIndex StructType::emitStoreParameter(OpCodeEmitter&, NomadId) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitStoreFunctionVariable(OpCodeEmitter&, NomadId) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitStoreDynamicVariable(OpCodeEmitter&, NomadId) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

NomadIndex StructType::emitStoreContextVariable(OpCodeEmitter&, NomadId) const {
    throw NomadException("Struct bytecode emission is not supported: " + getTypeName());
}

} // namespace nomad
