// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Type.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/String.hpp>

namespace nomad {

///////////////////////////////////////////////////////////////////////////////
// Type
void Type::initValue(RuntimeValue& /*value*/) const {
    // Default implementation does nothing
}

void Type::freeValue(RuntimeValue& /*value*/) const {
    // Default implementation does nothing
}

void Type::assignValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    RuntimeValue copy;
    initValue(copy);
    copyValue(sourceValue, copy);

    freeValue(destinationValue);
    destinationValue = copy;
}

const TypeOpCodes* Type::getTypeOpCodes() const {
    return &m_typeOpCodes;
}

bool Type::needsFree() const {
    return m_typeOpCodes.freeResult != nullptr;
}

bool Type::isReference() const {
    return false;
}

bool Type::acceptsArgument(const Type* argumentType) const {
    return sameType(argumentType);
}

bool Type::isInternal() const {
    return getTypeName().starts_with(FUNCTION_INTERNAL_NAME_PREFIX);
}

NomadString Type::toString(const RuntimeValue& value) const {
    NomadString text_value;

    toString(value, text_value);

    return text_value;
}

bool Type::sameType(const Type* other) const {
    if (other == nullptr) {
        return false;
    }

    if (other == this) {
        return true;
    }

    if (other->getTypeName() == getTypeName()) {
        return true;
    }

    return false;
}

const FunctionType* Type::asCallback() const {
    return nullptr;
}

const EventType * Type::asEvent() const {
    return nullptr;
}

bool Type::isVoid() const {
    return false;
}

bool Type::isString() const {
    return false;
}

///////////////////////////////////////////////////////////////////////////////
// VoidType
NomadString VoidType::getTypeName() const {
    return VOID_TYPE_NAME;
}

void VoidType::copyValue(const RuntimeValue& /*sourceValue*/, RuntimeValue& /*destinationValue*/) const {
    // Nothing to do...
}

void VoidType::toString(const RuntimeValue& /*value*/, NomadString& string) const {
    log::error("VoidType::to_string() called");
    string = "";
}

bool VoidType::isVoid() const {
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// IdType
NomadString IdType::getTypeName() const {
    return ID_TYPE_NAME;
}

void IdType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    destinationValue.setIdValue(sourceValue.getIdValue());
}

void IdType::toString(const RuntimeValue& value, NomadString& string) const {
    string = nomad::toString(value.getIdValue());
}

///////////////////////////////////////////////////////////////////////////////
// FloatType
NomadString FloatType::getTypeName() const {
    return FLOAT_TYPE_NAME;
}

void FloatType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    destinationValue.setFloatValue(sourceValue.getFloatValue());
}

void FloatType::toString(const RuntimeValue& value, NomadString& string) const {
    string = nomad::toString(value.getFloatValue());
}

///////////////////////////////////////////////////////////////////////////////
// IntegerType
NomadString IntegerType::getTypeName() const {
    return INTEGER_TYPE_NAME;
}

void IntegerType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    destinationValue.setIntegerValue(sourceValue.getIntegerValue());
}

void IntegerType::toString(const RuntimeValue& value, NomadString& string) const {
    string = nomad::toString(value.getIntegerValue());
}

///////////////////////////////////////////////////////////////////////////////
// BooleanType
NomadString BooleanType::getTypeName() const {
    return BOOLEAN_TYPE_NAME;
}

void BooleanType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    destinationValue.setIntegerValue(sourceValue.getIntegerValue());
}

void BooleanType::toString(const RuntimeValue& value, NomadString& string) const {
    auto boolean_value = value.getBooleanValue();

    string = nomad::toString(boolean_value);
}

///////////////////////////////////////////////////////////////////////////////
// BaseStringType
BaseStringType::BaseStringType() {
    m_typeOpCodes.initResult = op_string_init_r;
    m_typeOpCodes.initIntermediate = op_string_init_i;

    m_typeOpCodes.freeResult = op_string_free_r;
    m_typeOpCodes.freeIntermediate = op_string_free_i;
    m_typeOpCodes.freeStack = op_string_free_stack;
    m_typeOpCodes.freeFunctionVariable = op_string_free_variable;

    m_typeOpCodes.loadFunctionVariable = {
        op_function_variable_string_get_r, op_function_variable_string_get_i, op_function_variable_string_push
    };
    m_typeOpCodes.loadParameter = {
        op_string_parameter_load_r, op_string_parameter_load_i, op_string_parameter_push
    };
    m_typeOpCodes.loadDynamicVariable = {
        op_dynamic_variable_string_get_r, op_dynamic_variable_string_get_i, op_dynamic_variable_string_push
    };
    m_typeOpCodes.loadContextVariable = {
        op_context_variable_string_get_r, op_context_variable_string_get_i, op_context_variable_string_push
    };

    m_typeOpCodes.copyResultToStack = op_string_push_r;
    m_typeOpCodes.moveResultToIntermediate = op_string_move_r_to_i;
    m_typeOpCodes.moveResultToStack = op_string_move_r_to_stack;
    m_typeOpCodes.pushIntermediate = op_string_push_i;
    m_typeOpCodes.popIntermediate = op_string_pop_i;

    // The compiler frees the previous value, then `r` is moved into the variable.
    m_typeOpCodes.setFunctionVariable = op_function_variable_set;
    m_typeOpCodes.setDynamicVariable = op_dynamic_variable_string_set;
    m_typeOpCodes.setContextVariable = op_context_variable_string_set;
}

void BaseStringType::initValue(RuntimeValue& value) const {
    value.setNullStringValue();
}

void BaseStringType::freeValue(RuntimeValue& value) const {
    value.freeStringValue();
}

void BaseStringType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    destinationValue.setStringValue(sourceValue.getStringValue());
}

void BaseStringType::toString(const RuntimeValue& value, NomadString& string) const {
    const auto* text = value.getStringValue();
    string = text == nullptr ? "" : text;
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
// BaseStringRefType
void BaseStringRefType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    destinationValue.setStringRefValue(sourceValue.getStringValue());
}

void BaseStringRefType::toString(const RuntimeValue& value, NomadString& string) const {
    const auto* text = value.getStringValue();
    string = text == nullptr ? "" : text;
}

bool BaseStringRefType::isReference() const {
    return true;
}

bool BaseStringRefType::acceptsArgument(const Type* argumentType) const {
    return sameType(argumentType) || (argumentType != nullptr && argumentType->isString());
}

///////////////////////////////////////////////////////////////////////////////
// StringRefType
NomadString StringRefType::getTypeName() const {
    return STRING_REF_TYPE_NAME;
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
NomadString FunctionLineNumberType::getTypeName() const {
    return FUNCTION_LINE_NUMBER_TYPE_NAME;
}

void FunctionLineNumberType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    destinationValue.setIntegerValue(sourceValue.getIntegerValue());
}

void FunctionLineNumberType::toString(const RuntimeValue& value, NomadString& string) const {
    string = nomad::toString(value.getIntegerValue());
}

///////////////////////////////////////////////////////////////////////////////
// FunctionReferenceType
FunctionType::FunctionType(const std::vector<const Type*>& parameter_types, const Type* return_type):
    m_parameterTypes(parameter_types),
    m_returnType(return_type)
{
    m_name = FUNCTION_TYPE_NAME + "(";

    for (auto parameter_type : parameter_types) {
        m_name += parameter_type->getTypeName();

        if (parameter_type != parameter_types.back()) {
            m_name += ", ";
        }
    }

    m_name += "):" + return_type->getTypeName();
}

NomadString FunctionType::getTypeName() const {
    return FUNCTION_TYPE_NAME;
}

void FunctionType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    destinationValue.setIdValue(sourceValue.getIdValue());
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
// EventType
EventType::EventType(const EventDefinition* declaration):
    m_event(*declaration)
{}

NomadString EventType::getTypeName() const {
    return EVENT_TYPE_NAME;
}

void EventType::copyValue(const RuntimeValue &sourceValue, RuntimeValue &destinationValue) const {
    destinationValue.setIdValue(sourceValue.getIdValue());
}

void EventType::toString(const RuntimeValue & /*value*/, NomadString &string) const {
    string = m_event.name;

    if (m_event.parameters.empty() == false) {
        string += " ";

        for (const auto& parameterType : m_event.parameters) {
            string += parameterType.name;
            string += ":";
            string += parameterType.typeName;
        }
    }
}

bool EventType::sameType(const Type *other) const {
    return other == this;
}

const NomadString & EventType::getEventName() const {
    return m_event.name;
}


///////////////////////////////////////////////////////////////////////////////
// EventCallbackType
NomadString EventCallbackType::getTypeName() const {
    return EVENT_CALLBACK_TYPE_NAME;
}

void EventCallbackType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    destinationValue.setIdValue(sourceValue.getIdValue());
}

void EventCallbackType::toString(const RuntimeValue& value, NomadString& string) const {
    string = std::format("{}({})", EVENT_CALLBACK_TYPE_NAME, value.getIdValue());
}

///////////////////////////////////////////////////////////////////////////////
// EventDispatchType
NomadString EventDispatchType::getTypeName() const {
    return EVENT_DISPATCH_TYPE_NAME;
}

void EventDispatchType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    destinationValue.setIdValue(sourceValue.getIdValue());
}

void EventDispatchType::toString(const RuntimeValue& value, NomadString& string) const {
    string = std::format("{}({})", EVENT_DISPATCH_TYPE_NAME, value.getIdValue());
}

} // namespace nomad
