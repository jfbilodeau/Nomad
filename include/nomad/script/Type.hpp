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

// Opcodes loading a value into the `r` register, the `i` register or onto the stack.
struct TargetOpCodes {
    InstructionFn result = nullptr;
    InstructionFn intermediate = nullptr;
    InstructionFn stack = nullptr;
};

// Opcodes the compiler emits to initialize, copy, move and free values of a type. The defaults copy values bitwise and
// have nothing to initialize or free; types owning heap memory override them in their constructor.
// `nullptr` means there is nothing to emit.
struct TypeOpCodes {
    InstructionFn initResult = nullptr;
    InstructionFn initIntermediate = nullptr;

    InstructionFn freeResult = nullptr;
    InstructionFn freeIntermediate = nullptr;
    InstructionFn freeStack = nullptr;          // Operand: offset from the top of the stack (0 = top).
    InstructionFn freeFunctionVariable = nullptr; // Operand: function variable index.

    // Copy a stored value into `r`, `i` or onto the stack.
    TargetOpCodes loadFunctionVariable = {op_function_variable_get_r, op_function_variable_get_i, op_function_variable_push};
    TargetOpCodes loadParameter = {op_parameter_load_r, op_parameter_load_i, op_parameter_push};
    TargetOpCodes loadDynamicVariable = {op_dynamic_variable_get_r, op_dynamic_variable_get_i, op_dynamic_variable_push};
    TargetOpCodes loadContextVariable = {op_context_variable_get_r, op_context_variable_get_i, op_context_variable_push};

    // Register and stack traffic.
    InstructionFn copyResultToStack = op_push_r;
    InstructionFn moveResultToIntermediate = op_copy_r_to_i;
    InstructionFn moveIntermediateToResult = op_copy_i_to_r;
    InstructionFn moveResultToStack = op_push_r;
    InstructionFn pushIntermediate = op_push_i;
    InstructionFn popResult = op_pop_r;
    InstructionFn popIntermediate = op_pop_i;

    // Store the `r` register.
    InstructionFn setFunctionVariable = op_function_variable_set;
    InstructionFn setDynamicVariable = op_dynamic_variable_set;
    InstructionFn setContextVariable = op_context_variable_set;
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

    // Opcodes the compiler uses to manage the lifecycle of values of this type.
    [[nodiscard]] const TypeOpCodes* getTypeOpCodes() const;
    [[nodiscard]] bool needsFree() const;

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

protected:
    // Types owning heap memory replace the defaults in their constructor.
    TypeOpCodes m_typeOpCodes;

private:
    Runtime* m_runtime;
    const TypeId m_id;
    NomadString m_documentation;
};

[[nodiscard]] NomadString getTypeDisplayName(const Type* type);

} // namespace nomad
