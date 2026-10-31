// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/OpCode.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <nomad/Nomad.hpp>

namespace nomad {

DeclareId(TypeId);

class EventType;
class Runtime;
class FunctionType;
class EnumType;
class ClassType;
class CompositeType;

enum class TypeStorage {
    None = 1,  // for `void`
    Value,
    Object,
    Struct,
};

class Type {
public:
    explicit Type(Runtime* runtime, TypeId id, NomadString documentation = {});
    virtual ~Type() = default;

    [[nodiscard]] virtual NomadString getTypeName() const = 0;
    [[nodiscard]] virtual TypeStorage getStorage() const = 0;
    [[nodiscard]] virtual NomadIndex getSize() const = 0;
    [[nodiscard]] Runtime* getRuntime() const;
    [[nodiscard]] TypeId getId() const;
    [[nodiscard]] const NomadString& getDocumentation() const;
    // Storage points to getSize() contiguous RuntimeValue slots. Zero-size types access no slots.
    // Initializes cleared, unowned storage with an owned default value. Does not run script constructors.
    virtual void initValue(RuntimeValue* value) const = 0;
    virtual void freeValue(RuntimeValue* value) const;
    // Copies into empty RuntimeValue storage or replaces an owned value; does not require initValue first.
    virtual void copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const = 0;

    // Replace an owned value with a copy of `sourceValue`. Safe when `sourceValue` shares the destination's storage.
    void assignValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const;
    // Convert value to string.
    virtual void toString(const RuntimeValue& value, NomadString& string) const = 0;
    [[nodiscard]] NomadString toString(const RuntimeValue& value) const;

    [[nodiscard]] virtual bool needsFree() const;
    virtual void emitInitResult(OpCodeEmitter& emitter) const;
    virtual void emitInitIntermediate(OpCodeEmitter& emitter) const;
    virtual void emitFreeResult(OpCodeEmitter& emitter) const;
    virtual void emitFreeIntermediate(OpCodeEmitter& emitter) const;
    virtual void emitFreeStackValue(OpCodeEmitter& emitter, NomadIndex offset) const;
    virtual void emitFreeFunctionVariable(
        OpCodeEmitter& emitter,
        NomadIndex variableIndex
    ) const;
    virtual void emitFreeParameter(OpCodeEmitter& emitter, NomadId parameterId) const = 0;
    virtual NomadIndex emitLoadFunctionVariable(
        OpCodeEmitter& emitter,
        NomadId variableId,
        ExpressionTarget target
    ) const;
    virtual NomadIndex emitLoadParameter(
        OpCodeEmitter& emitter,
        NomadId parameterId,
        ExpressionTarget target
    ) const;
    virtual NomadIndex emitLoadDynamicVariable(
        OpCodeEmitter& emitter,
        NomadId variableId,
        ExpressionTarget target
    ) const;
    virtual NomadIndex emitLoadContextVariable(
        OpCodeEmitter& emitter,
        NomadId variableId,
        ExpressionTarget target
    ) const;
    virtual NomadIndex emitCopyResultToStack(OpCodeEmitter& emitter) const;
    virtual void emitMoveResultToIntermediate(OpCodeEmitter& emitter) const;
    virtual void emitMoveIntermediateToResult(OpCodeEmitter& emitter) const;
    virtual void emitMoveResultToStack(OpCodeEmitter& emitter) const;
    virtual NomadIndex emitPushIntermediate(OpCodeEmitter& emitter) const;
    virtual NomadIndex emitPopResult(OpCodeEmitter& emitter) const;
    virtual NomadIndex emitPopIntermediate(OpCodeEmitter& emitter) const;
    virtual NomadIndex emitStoreParameter(OpCodeEmitter& emitter, NomadId parameterId) const;
    virtual NomadIndex emitStoreFunctionVariable(OpCodeEmitter& emitter, NomadId variableId) const;
    virtual NomadIndex emitStoreDynamicVariable(OpCodeEmitter& emitter, NomadId variableId) const;
    virtual NomadIndex emitStoreContextVariable(OpCodeEmitter& emitter, NomadId variableId) const;

    // Can an expression of `argumentType` be passed where a value of this type is expected?
    [[nodiscard]] virtual bool acceptsArgument(const Type* argumentType) const;

    // Internal types (prefixed with `$`) cannot be declared in functions.
    [[nodiscard]] bool isInternal() const;

    // Can the type hold a value or is it void?
    [[nodiscard]] virtual bool isVoid() const;

    // Is the type a string type?
    [[nodiscard]] virtual bool isString() const;

    // Determine if two types are the same
    [[nodiscard]] virtual bool sameType(const Type* other) const;

    // Return the type as a callback type or `nullptr` if it is not a callback
    [[nodiscard]] virtual const FunctionType* asCallback() const;
    [[nodiscard]] virtual const EventType* asEvent() const;
    [[nodiscard]] virtual const EnumType* asEnum() const;
    [[nodiscard]] virtual const ClassType* asClassType() const;
    [[nodiscard]] virtual const CompositeType* asCompositeType() const;

private:
    Runtime* m_runtime;
    const TypeId m_id;
    NomadString m_documentation;
};

[[nodiscard]] NomadString getTypeDisplayName(const Type* type);

} // namespace nomad
