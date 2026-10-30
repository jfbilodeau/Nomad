// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#ifndef NOMAD_TYPE_HPP
#define NOMAD_TYPE_HPP

#include <nomad/script/RuntimeValue.hpp>

#include <nomad/Nomad.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/NativeMember.hpp>
#include <nomad/script/OpCode.hpp>
#include <nomad/script/Property.hpp>
#include <nomad/script/RecordType.hpp>
#include <nomad/script/TypeBase.hpp>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace nomad {

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

class ClassType : public RecordType {
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

    void initValue(RuntimeValue& value) const override;
    void freeValue(RuntimeValue& value) const override;
    void copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const override;
    [[nodiscard]] bool sameType(const Type* other) const override;

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
