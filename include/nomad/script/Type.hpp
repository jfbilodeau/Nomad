// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#ifndef NOMAD_TYPE_HPP
#define NOMAD_TYPE_HPP

#include <nomad/script/RuntimeValue.hpp>

#include <nomad/Nomad.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/NativeMember.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/Property.hpp>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace nomad {

DeclareId(TypeId);

// Forward declarations
class EventType;
class Runtime;
class FunctionType;
class EnumType;
class Constructor;
class Method;
class ClassType;

enum class TypeStorage {
    None = 1,  // for `void`
    Value,
    Object,
    Struct,
};

enum class ClassMode {
    Opened = 1,
    Closed,
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
    [[nodiscard]] virtual const ClassType* asClassType() const;

protected:
    // Types owning heap memory replace the defaults in their constructor.
    TypeOpCodes m_typeOpCodes;

private:
    Runtime* m_runtime;
    const TypeId m_id;
    NomadString m_documentation;
};

class ValueType : public Type {
public:
    explicit ValueType(Runtime* runtime, TypeId id, NomadString documentation = {});
    [[nodiscard]] TypeStorage getStorage() const override;
    [[nodiscard]] NomadIndex getSize() const override;
};

[[nodiscard]] NomadString getTypeDisplayName(const Type* type);

const NomadString VOID_TYPE_NAME = "void";
class VoidType final : public Type {
public:
    using Type::Type;
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    [[nodiscard]] TypeStorage getStorage() const override;
    [[nodiscard]] NomadIndex getSize() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;

    [[nodiscard]] bool isVoid() const override;
};

const NomadString ID_TYPE_NAME = "id";
class IdType final : public ValueType {
public:
    using ValueType::ValueType;
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

class OpaqueIdType final : public ValueType {
public:
    void initValue(RuntimeValue& value) const override;
    OpaqueIdType(Runtime* runtime, TypeId id, NomadString name, NomadString documentation);

    using Type::toString;

    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;

private:
    NomadString m_name;
};

const NomadString INTEGER_TYPE_NAME = "int";
class IntegerType final : public ValueType {
public:
    using ValueType::ValueType;
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

struct EnumValueDefinition {
    NomadString name;
    NomadInteger value;
};

class EnumType final : public ValueType {
public:
    void initValue(RuntimeValue& value) const override;
    EnumType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        std::vector<EnumValueDefinition> values,
        NomadString documentation = {}
    );

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
class FloatType final : public ValueType {
public:
    using ValueType::ValueType;
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString BOOLEAN_TYPE_NAME = "bool";
const NomadString BOOLEAN_TRUE_STRING = "true";
const NomadString BOOLEAN_FALSE_STRING = "false";
class BooleanType final : public ValueType {
public:
    using ValueType::ValueType;
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

struct IterableInfo {
    const Type* valueType;
    const ClassType* iteratorType;
    CallableId factoryId;
};

class ClassType : public Type {
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
    // The name of the share member initializer called
    // by each constructor.
    [[nodiscard]] const NomadString& getInitializerName() const;
    using Type::toString;
    [[nodiscard]] NomadString getTypeName() const override;
    [[nodiscard]] TypeStorage getStorage() const override;
    [[nodiscard]] NomadIndex getSize() const override;
    [[nodiscard]] const std::vector<const Type*>& getTypeArguments() const;
    [[nodiscard]] const ClassType* asClassType() const override;
    [[nodiscard]] CallableId registerConstructor(
        Runtime& runtime,
        std::unique_ptr<Constructor> constructor
    );
    [[nodiscard]] std::vector<const Constructor*> getConstructors() const;
    [[nodiscard]] CallableId registerMethod(
        Runtime& runtime,
        std::unique_ptr<Method> method
    );
    [[nodiscard]] std::vector<const Method*> getMethods() const;
    void toString(const RuntimeValue& value, NomadString& string) const override;

    void initValue(RuntimeValue& value) const override;
    void freeValue(RuntimeValue& value) const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] PropertyId registerProperty(
        Runtime& runtime,
        std::unique_ptr<Property> property
    );
    [[nodiscard]] const Property* getProperty(const NomadString& name) const;
    [[nodiscard]] const Property* getProperty(PropertyId id) const;
    [[nodiscard]] std::vector<const Property*> getProperties() const;
    [[nodiscard]] NomadIndex getFieldIndex(PropertyId id) const;
    [[nodiscard]] virtual NomadIndex getPropertyCount() const;
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
    NomadString m_name;
    NomadString m_initializerName;
    ClassMode m_mode;
    std::vector<const Type*> m_typeArguments;
    std::vector<std::unique_ptr<Property>> m_properties;
    std::vector<std::unique_ptr<Constructor>> m_constructors;
    std::vector<std::unique_ptr<Method>> m_methods;
    std::vector<std::unique_ptr<NativeIndexerInfo>> m_nativeIndexers;
    std::optional<IterableInfo> m_iterableInfo;
};

class BaseStringType : public ClassType {
public:
    explicit BaseStringType(Runtime* runtime, TypeId id);

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
    using BaseStringType::BaseStringType;
    [[nodiscard]] NomadString getTypeName() const override;
};

// Internal types

// Hidden parameter: the compiler passes the calling function's file name.
const NomadString FILE_NAME_TYPE_NAME = "$file";
class FileNameType final : public BaseStringType {
public:
    using BaseStringType::BaseStringType;
    [[nodiscard]] NomadString getTypeName() const override;
};

// Hidden parameter: the compiler passes the calling function's name.
const NomadString FUNCTION_NAME_TYPE_NAME = "$function";
class FunctionNameType final : public BaseStringType {
public:
    using BaseStringType::BaseStringType;
    [[nodiscard]] NomadString getTypeName() const override;
};

const NomadString FUNCTION_LINE_NUMBER_TYPE_NAME = "$line";
class FunctionLineNumberType final : public ValueType {
public:
    using ValueType::ValueType;
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString FUNCTION_TYPE_NAME = "callback";
class FunctionType final : public ValueType {
public:
    void initValue(RuntimeValue& value) const override;
    FunctionType(
        Runtime* runtime,
        TypeId id,
        const std::vector<const Type*>& parameterTypes,
        const Type* returnType
    );

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

const NomadString EVENT_CALLBACK_TYPE_NAME = "eventCallback";
class EventCallbackType final : public ValueType {
public:
    using ValueType::ValueType;
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

const NomadString EVENT_DISPATCH_TYPE_NAME = "eventDispatch";
class EventDispatchType final : public ValueType {
public:
    using ValueType::ValueType;
    void initValue(RuntimeValue& value) const override;
    [[nodiscard]] NomadString getTypeName() const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    void toString(const RuntimeValue& value, NomadString& string) const override;
};

} // nomad

#endif // NOMAD_TYPE_HPP
