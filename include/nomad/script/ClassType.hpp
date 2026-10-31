// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/CompositeType.hpp>
#include <nomad/script/NativeMember.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace nomad {

struct IterableInfo {
    const Type* valueType;
    const ClassType* iteratorType;
    CallableId factoryId;
};

class ClassType : public CompositeType {
public:
    explicit ClassType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        std::vector<const Type*> typeArguments = {},
        NomadString documentation = {}
    );
    ~ClassType() override;
    using Type::toString;
    [[nodiscard]] TypeStorage getStorage() const override;
    [[nodiscard]] NomadIndex getSize() const override;
    [[nodiscard]] const std::vector<const Type*>& getTypeArguments() const;
    [[nodiscard]] const ClassType* asClassType() const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;

    void initValue(RuntimeValue* value) const override;
    void freeValue(RuntimeValue* value) const override;
    void copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;
    [[nodiscard]] bool needsFree() const override;
    void emitInitResult(OpCodeEmitter& emitter) const override;
    void emitInitIntermediate(OpCodeEmitter& emitter) const override;
    void emitFreeResult(OpCodeEmitter& emitter) const override;
    void emitFreeIntermediate(OpCodeEmitter& emitter) const override;
    void emitFreeStackValue(OpCodeEmitter& emitter, NomadIndex offset) const override;
    void emitFreeFunctionVariable(
        OpCodeEmitter& emitter,
        NomadIndex variableIndex
    ) const override;
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

    [[nodiscard]] IndexerId registerNativeIndexer(
        Runtime& runtime,
        NativeIndexerInfo info
    );
    [[nodiscard]] const NativeIndexerInfo* getNativeIndexer(NomadIndex indexCount) const;
    [[nodiscard]] std::vector<const NativeIndexerInfo*> getNativeIndexers() const;
    void registerIterableType(
        Runtime& runtime,
        const Type* valueType,
        const Method* factory
    );
    [[nodiscard]] std::optional<IterableInfo> getIterableInfo() const;

private:
    std::vector<const Type*> m_typeArguments;
    std::vector<std::unique_ptr<NativeIndexerInfo>> m_nativeIndexers;
    std::optional<IterableInfo> m_iterableInfo;
};

} // namespace nomad
