// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/CompositeType.hpp>

namespace nomad {

class StructType final : public CompositeType {
public:
    StructType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        NomadString documentation = {}
    );

    using Type::toString;
    [[nodiscard]] TypeStorage getStorage() const override;
    [[nodiscard]] NomadIndex getSize() const override;
    [[nodiscard]] bool sameType(const Type* other) const override;
    [[nodiscard]] const StructType* asStructType() const override;
    [[nodiscard]] bool needsFree() const override;
    void initValue(RuntimeValue* value) const override;
    void copyValue(const RuntimeValue* source, RuntimeValue* destination) const override;
    void freeValue(RuntimeValue* value) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;

    void emitInitResult(OpCodeEmitter& emitter) const override;
    void emitInitIntermediate(OpCodeEmitter& emitter) const override;
    void emitFreeResult(OpCodeEmitter& emitter) const override;
    void emitFreeIntermediate(OpCodeEmitter& emitter) const override;
    void emitFreeStackValue(OpCodeEmitter& emitter, NomadIndex offset) const override;
    void emitFreeFunctionVariable(OpCodeEmitter& emitter, NomadIndex variableIndex) const override;
    void emitFreeParameter(OpCodeEmitter& emitter, NomadId parameterId) const override;
    NomadIndex emitLoadFunctionVariable(
        OpCodeEmitter& emitter,
        NomadId variableId,
        ExpressionTarget target
    ) const override;
    NomadIndex emitLoadParameter(
        OpCodeEmitter& emitter,
        NomadId parameterId,
        ExpressionTarget target
    ) const override;
    NomadIndex emitLoadDynamicVariable(
        OpCodeEmitter& emitter,
        NomadId variableId,
        ExpressionTarget target
    ) const override;
    NomadIndex emitLoadContextVariable(
        OpCodeEmitter& emitter,
        NomadId variableId,
        ExpressionTarget target
    ) const override;
    NomadIndex emitCopyResultToStack(OpCodeEmitter& emitter) const override;
    void emitMoveResultToIntermediate(OpCodeEmitter& emitter) const override;
    void emitMoveIntermediateToResult(OpCodeEmitter& emitter) const override;
    void emitMoveResultToStack(OpCodeEmitter& emitter) const override;
    NomadIndex emitPushIntermediate(OpCodeEmitter& emitter) const override;
    NomadIndex emitPopResult(OpCodeEmitter& emitter) const override;
    NomadIndex emitPopIntermediate(OpCodeEmitter& emitter) const override;
    NomadIndex emitStoreParameter(OpCodeEmitter& emitter, NomadId parameterId) const override;
    NomadIndex emitStoreFunctionVariable(OpCodeEmitter& emitter, NomadId variableId) const override;
    NomadIndex emitStoreDynamicVariable(OpCodeEmitter& emitter, NomadId variableId) const override;
    NomadIndex emitStoreContextVariable(OpCodeEmitter& emitter, NomadId variableId) const override;
};

} // namespace nomad
