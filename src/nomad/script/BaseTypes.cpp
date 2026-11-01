// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/BaseTypes.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/system/String.hpp>

#include <format>
#include <utility>

namespace nomad {

///////////////////////////////////////////////////////////////////////////////
// VoidType
void VoidType::initValue(RuntimeValue* /*value*/) const {
}

NomadString VoidType::getTypeName() const {
    return VOID_TYPE_NAME;
}

TypeStorage VoidType::getStorage() const {
    return TypeStorage::None;
}

NomadIndex VoidType::getSize() const {
    return 0;
}

void VoidType::copyValue(const RuntimeValue* /*sourceValue*/, RuntimeValue* /*destinationValue*/) const {
    // Nothing to do...
}

void VoidType::toString(const RuntimeValue& /*value*/, NomadString& string) const {
    log::error("VoidType::to_string() called");
    string = "";
}

bool VoidType::isVoid() const {
    return true;
}

void VoidType::emitFreeParameter(OpCodeEmitter&, NomadIndex) const {
}

///////////////////////////////////////////////////////////////////////////////
// IdType
void IdType::initValue(RuntimeValue* value) const {
    value->setIdValue(0);
}

NomadString IdType::getTypeName() const {
    return ID_TYPE_NAME;
}

void IdType::copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const {
    destinationValue->setIdValue(sourceValue->getIdValue());
}

void IdType::toString(const RuntimeValue& value, NomadString& string) const {
    string = nomad::toString(value.getIdValue());
}

///////////////////////////////////////////////////////////////////////////////
// OpaqueIdType
OpaqueIdType::OpaqueIdType(Runtime* runtime, TypeId id, NomadString name, NomadString documentation):
    ValueType(runtime, id, std::move(documentation)),
    m_name(std::move(name))
{
}

void OpaqueIdType::initValue(RuntimeValue* value) const {
    value->setIdValue(0);
}

NomadString OpaqueIdType::getTypeName() const {
    return m_name;
}

void OpaqueIdType::copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const {
    destinationValue->setIdValue(sourceValue->getIdValue());
}

void OpaqueIdType::toString(const RuntimeValue& value, NomadString& string) const {
    string = m_name + "(" + nomad::toString(value.getIdValue()) + ")";
}

bool OpaqueIdType::sameType(const Type* other) const {
    return other == this;
}

///////////////////////////////////////////////////////////////////////////////
// FloatType
void FloatType::initValue(RuntimeValue* value) const {
    value->setFloatValue(0.0f);
}

NomadString FloatType::getTypeName() const {
    return FLOAT_TYPE_NAME;
}

void FloatType::copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const {
    destinationValue->setFloatValue(sourceValue->getFloatValue());
}

void FloatType::toString(const RuntimeValue& value, NomadString& string) const {
    string = nomad::toString(value.getFloatValue());
}

///////////////////////////////////////////////////////////////////////////////
// IntegerType
void IntegerType::initValue(RuntimeValue* value) const {
    value->setIntegerValue(0);
}

NomadString IntegerType::getTypeName() const {
    return INTEGER_TYPE_NAME;
}

void IntegerType::copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const {
    destinationValue->setIntegerValue(sourceValue->getIntegerValue());
}

void IntegerType::toString(const RuntimeValue& value, NomadString& string) const {
    string = nomad::toString(value.getIntegerValue());
}

///////////////////////////////////////////////////////////////////////////////
// BooleanType
void BooleanType::initValue(RuntimeValue* value) const {
    value->setBooleanValue(NOMAD_FALSE);
}

NomadString BooleanType::getTypeName() const {
    return BOOLEAN_TYPE_NAME;
}

void BooleanType::copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const {
    destinationValue->setIntegerValue(sourceValue->getIntegerValue());
}

void BooleanType::toString(const RuntimeValue& value, NomadString& string) const {
    auto boolean_value = value.getBooleanValue();

    string = nomad::toString(boolean_value);
}

///////////////////////////////////////////////////////////////////////////////
// BaseStringType
BaseStringType::BaseStringType(Runtime* runtime, TypeId id):
    ClassType(runtime, id, STRING_TYPE_NAME, ClassMode::Closed)
{
}

void BaseStringType::initValue(RuntimeValue* value) const {
    value->initStringValue();
}

void BaseStringType::emitInitResult(OpCodeEmitter& emitter) const {
    emitter.addOpCode(op_string_init_r);
}

void BaseStringType::emitInitIntermediate(OpCodeEmitter& emitter) const {
    emitter.addOpCode(op_string_init_i);
}

void BaseStringType::emitInitFunctionVariable(
    OpCodeEmitter& emitter,
    const NomadIndex offset
) const {
    emitInitResult(emitter);
    emitStoreFunctionVariable(emitter, offset);
}

bool BaseStringType::sameType(const Type* other) const {
    return Type::sameType(other);
}

void BaseStringType::toString(const RuntimeValue& value, NomadString& string) const {
    string = value.getStringValue();
}

bool BaseStringType::isString() const {
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// StringType
NomadString StringType::getTypeName() const {
    return STRING_TYPE_NAME;
}

///////////////////////////////////////////////////////////////////////////////
// FileNameType
NomadString FileNameType::getTypeName() const {
    return FILE_NAME_TYPE_NAME;
}

///////////////////////////////////////////////////////////////////////////////
// FunctionNameType
NomadString FunctionNameType::getTypeName() const {
    return FUNCTION_NAME_TYPE_NAME;
}

///////////////////////////////////////////////////////////////////////////////
// FunctionLineNumberType
void FunctionLineNumberType::initValue(RuntimeValue* value) const {
    value->setIntegerValue(0);
}

NomadString FunctionLineNumberType::getTypeName() const {
    return FUNCTION_LINE_NUMBER_TYPE_NAME;
}

void FunctionLineNumberType::copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const {
    destinationValue->setIntegerValue(sourceValue->getIntegerValue());
}

void FunctionLineNumberType::toString(const RuntimeValue& value, NomadString& string) const {
    string = nomad::toString(value.getIntegerValue());
}

///////////////////////////////////////////////////////////////////////////////
// FunctionReferenceType
FunctionType::FunctionType(
    Runtime* runtime,
    TypeId id,
    const std::vector<const Type*>& parameterTypes,
    const Type* returnType
):
    ValueType(runtime, id),
    m_parameterTypes(parameterTypes),
    m_returnType(returnType)
{
    m_name = FUNCTION_TYPE_NAME + "(";

    for (const auto* parameterType : parameterTypes) {
        m_name += parameterType->getTypeName();

        if (parameterType != parameterTypes.back()) {
            m_name += ", ";
        }
    }

    m_name += "):" + returnType->getTypeName();
}

void FunctionType::initValue(RuntimeValue* value) const {
    value->setIdValue(0);
}

NomadString FunctionType::getTypeName() const {
    return FUNCTION_TYPE_NAME;
}

void FunctionType::copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const {
    destinationValue->setIdValue(sourceValue->getIdValue());
}

void FunctionType::toString(const RuntimeValue& value, NomadString& string) const {
    string = nomad::toString(value.getIdValue());
}

NomadIndex FunctionType::getParameterCount() const {
    return m_parameterTypes.size();
}

const Type* FunctionType::getParameterType(NomadIndex index) const {
    return m_parameterTypes[index];
}

const Type* FunctionType::getReturnType() const {
    return m_returnType;
}

bool FunctionType::sameType(const Type* other) const {
    auto other_callback = other->asCallback();

    // Probably not necessary
    if (other_callback == nullptr) {
        return false;
    }

    if (other_callback->getReturnType() != getReturnType()) {
        return false;
    }

    if (other_callback->getParameterCount() != getParameterCount()) {
        return false;
    }

    for (NomadIndex i = 0; i < getParameterCount(); ++i) {
        if (other_callback->getParameterType(i) != getParameterType(i)) {
            return false;
        }
    }

    return true;
}

const FunctionType* FunctionType::asCallback() const {
    return this;
}

///////////////////////////////////////////////////////////////////////////////
// EventCallbackType
void EventCallbackType::initValue(RuntimeValue* value) const {
    value->setIdValue(0);
}

NomadString EventCallbackType::getTypeName() const {
    return EVENT_CALLBACK_TYPE_NAME;
}

void EventCallbackType::copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const {
    destinationValue->setIdValue(sourceValue->getIdValue());
}

void EventCallbackType::toString(const RuntimeValue& value, NomadString& string) const {
    string = std::format("{}({})", EVENT_CALLBACK_TYPE_NAME, value.getIdValue());
}

///////////////////////////////////////////////////////////////////////////////
// EventDispatchType
void EventDispatchType::initValue(RuntimeValue* value) const {
    value->setIdValue(0);
}

NomadString EventDispatchType::getTypeName() const {
    return EVENT_DISPATCH_TYPE_NAME;
}

void EventDispatchType::copyValue(const RuntimeValue* sourceValue, RuntimeValue* destinationValue) const {
    destinationValue->setIdValue(sourceValue->getIdValue());
}

void EventDispatchType::toString(const RuntimeValue& value, NomadString& string) const {
    string = std::format("{}({})", EVENT_DISPATCH_TYPE_NAME, value.getIdValue());
}

} // namespace nomad
