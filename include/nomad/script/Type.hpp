// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#ifndef NOMAD_TYPE_HPP
#define NOMAD_TYPE_HPP

#include <nomad/script/Event.hpp>

#include <nomad/script/RuntimeValue.hpp>

#include <nomad/Nomad.hpp>

#include <nomad/script/OpCode.hpp>

#include <vector>

namespace nomad {

// Forward declarations
class EventType;
class Runtime;
class FunctionType;

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
    InstructionFn moveResultToStack = op_push_r;
    InstructionFn pushIntermediate = op_push_i;
    InstructionFn popIntermediate = op_pop_i;

    // Store the `r` register.
    InstructionFn setFunctionVariable = op_function_variable_set;
    InstructionFn setDynamicVariable = op_dynamic_variable_set;
    InstructionFn setContextVariable = op_context_variable_set;
};

class Type {
public:
    virtual ~Type() = default;

    [[nodiscard]] virtual NomadString getTypeName() const = 0;
    virtual void initValue(RuntimeValue& value) const;
    virtual void freeValue(RuntimeValue& value) const;
    virtual void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const = 0;

    // Replace an owned value with a copy of `sourceValue`. Safe when `sourceValue` shares the destination's storage.
    void assignValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const;
    // Convert value to string.
    virtual void toString(const RuntimeValue& value, NomadString& string) const = 0;
    [[nodiscard]] NomadString toString(const RuntimeValue& value) const;

    // Opcodes the compiler uses to manage the lifecycle of values of this type.
    [[nodiscard]] const TypeOpCodes* getTypeOpCodes() const;
    [[nodiscard]] bool needsFree() const;

    // Borrowed types (i.e. `$stringref`) never own their value. They are only used for nativeFunction parameters and
    // operator operands, and are never stored.
    [[nodiscard]] virtual bool isReference() const;

    // Can an expression of `argumentType` be passed where a value of this type is expected?
    [[nodiscard]] virtual bool acceptsArgument(const Type* argumentType) const;

    // Internal types (prefixed with `$`) cannot be declared in functions.
    [[nodiscard]] bool isInternal() const;

    // Can the type hold a value or is it void?
    [[nodiscard]] virtual bool isVoid() const;

    // Is the type an owned string type? ie: `string`, `$function`
    [[nodiscard]] virtual bool isString() const;

    // Determine if two types are the same
    [[nodiscard]] virtual bool sameType(const Type* other) const;

    // Return the type as a callback type or `nullptr` if it is not a callback
    [[nodiscard]] virtual const FunctionType* asCallback() const;
    [[nodiscard]] virtual const EventType* asEvent() const;

protected:
    // Types owning heap memory replace the defaults in their constructor.
    TypeOpCodes m_typeOpCodes;
};

const NomadString VOID_TYPE_NAME = "void";
class VoidType final : public Type {
public:
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;

    [[nodiscard]] bool isVoid() const override;
};

const NomadString ID_TYPE_NAME = "id";
class IdType final : public Type {
public:
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString INTEGER_TYPE_NAME = "int";
class IntegerType final : public Type {
public:
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString FLOAT_TYPE_NAME = "float";
class FloatType final : public Type {
public:
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString BOOLEAN_TYPE_NAME = "bool";
const NomadString BOOLEAN_TRUE_STRING = "true";
const NomadString BOOLEAN_FALSE_STRING = "false";
class BooleanType final : public Type {
public:
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

// Base string type.
class BaseStringType : public Type {
public:
    BaseStringType();

    void initValue(RuntimeValue& value) const override;
    void freeValue(RuntimeValue& value) const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]]
    bool isString() const final;
};

const NomadString STRING_TYPE_NAME = "string";
class StringType final : public BaseStringType {
public:
    [[nodiscard]] NomadString getTypeName() const override;
};

// Internal types

// Borrowed pointer to a string owned by someone else (string table, parameter, variable or a temporary held by the
// caller). Valid only for the duration of the nativeFunction call or operator using it. Copying is shallow; nothing is freed.
class BaseStringRefType : public Type {
public:
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]] bool isReference() const final;
    [[nodiscard]] bool acceptsArgument(const Type* argumentType) const override;
};

const NomadString STRING_REF_TYPE_NAME = "$stringref";
class StringRefType final : public BaseStringRefType {
public:
    [[nodiscard]] NomadString getTypeName() const override;
};

// Hidden parameter: the compiler passes a reference to the calling function's file name.
const NomadString FILE_NAME_TYPE_NAME = "$file";
class FileNameType final : public BaseStringRefType {
public:
    [[nodiscard]] NomadString getTypeName() const override;
};

// Hidden parameter: the compiler passes a reference to the calling function's name.
const NomadString FUNCTION_NAME_TYPE_NAME = "$function";
class FunctionNameType final : public BaseStringRefType {
public:
    [[nodiscard]] NomadString getTypeName() const override;
};

const NomadString FUNCTION_LINE_NUMBER_TYPE_NAME = "$line";
class FunctionLineNumberType final : public Type {
public:
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString FUNCTION_TYPE_NAME = "callback";
class FunctionType final : public Type {
public:
    FunctionType(const std::vector<const Type*>& parameter_types, const Type* return_type);

    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;

    [[nodiscard]] NomadIndex getParameterCount() const;
    [[nodiscard]] const Type* getParameterType(NomadIndex index) const;
    [[nodiscard]] const Type* getReturnType() const;

    [[nodiscard]] const FunctionType* asCallback() const override;

private:
    NomadString m_name;
    const std::vector<const Type*> m_parameterTypes;
    const Type* m_returnType;
};

const NomadString EVENT_TYPE_NAME = "event";
class EventType final : public Type {
public:
    explicit EventType(const EventDefinition* declaration);

    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;

    [[nodiscard]] const NomadString& getEventName() const;

private:
    const EventDefinition m_event;
};

const NomadString EVENT_CALLBACK_TYPE_NAME = "eventCallback";
class EventCallbackType final : public Type {
public:
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString EVENT_DISPATCH_TYPE_NAME = "eventDispatch";
class EventDispatchType final : public Type {
public:
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString INDEXER_TYPE_NAME = "indexer";
class IndexerType final : public Type {
public:
    IndexerType(const std::vector<Type*>& keyTypes, const Type* valueType);

    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;
};

const NomadString MAP_TYPE_NAME = "map";
class MapType final : public Type {
public:
    MapType(const Type* keyType, const Type* valueType);

    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;

    [[nodiscard]] const Type* getKeyType() const;
    [[nodiscard]] const Type* getValueType() const;

private:
    const Type* m_keyType;
    const Type* m_valueType;
};

const NomadString ARRAY_TYPE_NAME = "array";
class ArrayType final : public Type {
public:
    ArrayType(NomadIndex dimension, const Type* valueType);

    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;

    [[nodiscard]] const Type* getDimension() const;
    [[nodiscard]] const Type* getValueType() const;

private:
    const NomadIndex m_dimension;
    const Type* m_valueType;
};

} // nomad

#endif // NOMAD_TYPE_HPP
