// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#ifndef NOMAD_TYPE_HPP
#define NOMAD_TYPE_HPP

#include <nomad/script/Event.hpp>

#include <nomad/script/RuntimeValue.hpp>

#include <nomad/Nomad.hpp>

#include <nomad/script/OpCode.hpp>

#include <functional>
#include <span>
#include <vector>

namespace nomad {

// Forward declarations
class EventType;
class Runtime;
class FunctionType;
class EnumType;

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
    explicit Type(NomadString documentation = {});
    virtual ~Type() = default;

    [[nodiscard]] virtual NomadString getTypeName() const = 0;
    [[nodiscard]] const NomadString& getDocumentation() const;
    // Initializes fresh storage with an owned default value. Does not release existing contents.
    virtual void initValue(RuntimeValue& value) const = 0;
    virtual void freeValue(RuntimeValue& value) const;
    // Copies into empty RuntimeValue storage or replaces an owned value; does not require initValue first.
    virtual void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const = 0;

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

protected:
    // Types owning heap memory replace the defaults in their constructor.
    TypeOpCodes m_typeOpCodes;

private:
    NomadString m_documentation;
};

[[nodiscard]] NomadString getTypeDisplayName(const Type* type);

const NomadString VOID_TYPE_NAME = "void";
class VoidType final : public Type {
public:
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;

    [[nodiscard]] bool isVoid() const override;
};

const NomadString ID_TYPE_NAME = "id";
class IdType final : public Type {
public:
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

class OpaqueIdType final : public Type {
public:
    void initValue(RuntimeValue& value) const override;
    OpaqueIdType(NomadString name, NomadString documentation);

    using Type::toString;

    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;

private:
    NomadString m_name;
};

const NomadString INTEGER_TYPE_NAME = "int";
class IntegerType final : public Type {
public:
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

struct EnumValueDefinition {
    NomadString name;
    NomadInteger value;
};

class EnumType final : public Type {
public:
    void initValue(RuntimeValue& value) const override;
    EnumType(NomadString name, std::vector<EnumValueDefinition> values);

    using Type::toString;

    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;
    [[nodiscard]] const EnumType* asEnum() const override;

    [[nodiscard]] NomadIndex getValueCount() const;
    [[nodiscard]] const EnumValueDefinition& getValue(NomadIndex index) const;
    [[nodiscard]] bool getValueByName(const NomadString& name, NomadInteger& value) const;
    [[nodiscard]] bool getNameByValue(NomadInteger value, NomadString& name) const;

private:
    NomadString m_name;
    std::vector<EnumValueDefinition> m_values;
};

const NomadString FLOAT_TYPE_NAME = "float";
class FloatType final : public Type {
public:
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString BOOLEAN_TYPE_NAME = "bool";
const NomadString BOOLEAN_TRUE_STRING = "true";
const NomadString BOOLEAN_FALSE_STRING = "false";
class BooleanType final : public Type {
public:
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

class ObjectTypeBase : public Type {
public:
    explicit ObjectTypeBase(NomadString documentation = {});
    [[nodiscard]] virtual bool isConstructible() const;
    // Returns a new object with one owned reference.
    [[nodiscard]] virtual NomadObject* createObject() const;
    [[nodiscard]] virtual std::span<const Type* const> getConstructorParameterTypes() const;
    [[nodiscard]] virtual NomadObject* createObject(std::span<const RuntimeValue> arguments) const;
    void initValue(RuntimeValue& value) const override;
    void freeValue(RuntimeValue& value) const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;
};

class ObjectType : public ObjectTypeBase {
public:
    using ObjectFactory = std::function<NomadObject*()>;
    explicit ObjectType(
        NomadString name,
        std::vector<const Type*> typeArguments = {},
        ObjectFactory factory = {},
        NomadString documentation = {}
    );
    [[nodiscard]] bool isConstructible() const override;
    [[nodiscard]] NomadObject* createObject() const override;
    using Type::toString;
    [[nodiscard]] NomadString getTypeName() const override;
    [[nodiscard]] const std::vector<const Type*>& getTypeArguments() const;
    void toString(const RuntimeValue& value, NomadString& string) const override;

private:
    NomadString m_name;
    std::vector<const Type*> m_typeArguments;
    ObjectFactory m_factory;
};

class BaseStringType : public ObjectTypeBase {
public:
    BaseStringType();

    using Type::toString;
    void initValue(RuntimeValue& value) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;
    [[nodiscard]]
    bool isString() const final;
};

const NomadString STRING_TYPE_NAME = "string";
class StringType final : public BaseStringType {
public:
    [[nodiscard]] NomadString getTypeName() const override;
};

// Internal types

// Hidden parameter: the compiler passes the calling function's file name.
const NomadString FILE_NAME_TYPE_NAME = "$file";
class FileNameType final : public BaseStringType {
public:
    [[nodiscard]] NomadString getTypeName() const override;
};

// Hidden parameter: the compiler passes the calling function's name.
const NomadString FUNCTION_NAME_TYPE_NAME = "$function";
class FunctionNameType final : public BaseStringType {
public:
    [[nodiscard]] NomadString getTypeName() const override;
};

const NomadString FUNCTION_LINE_NUMBER_TYPE_NAME = "$line";
class FunctionLineNumberType final : public Type {
public:
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString FUNCTION_TYPE_NAME = "callback";
class FunctionType final : public Type {
public:
    void initValue(RuntimeValue& value) const override;
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
    void initValue(RuntimeValue& value) const override;
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
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString EVENT_DISPATCH_TYPE_NAME = "eventDispatch";
class EventDispatchType final : public Type {
public:
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString INDEXER_TYPE_NAME = "indexer";
class IndexerType final : public Type {
public:
    void initValue(RuntimeValue& value) const override;
    IndexerType(const std::vector<Type*>& keyTypes, const Type* valueType);

    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;
};

} // nomad

#endif // NOMAD_TYPE_HPP
