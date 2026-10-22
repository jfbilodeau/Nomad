// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Type.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/Runtime.hpp>

#include <nomad/system/String.hpp>

#include <algorithm>

namespace nomad {

///////////////////////////////////////////////////////////////////////////////
// Type
Type::Type(NomadString documentation):
    m_documentation(std::move(documentation))
{
}

const NomadString& Type::getDocumentation() const {
    return m_documentation;
}

void Type::freeValue(RuntimeValue& /*value*/) const {
    // Default implementation does nothing
}

void Type::assignValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    RuntimeValue copy;
    try {
        copyValue(sourceValue, copy);
    } catch (...) {
        freeValue(copy);
        throw;
    }

    freeValue(destinationValue);
    destinationValue = copy;
}

const TypeOpCodes* Type::getTypeOpCodes() const {
    return &m_typeOpCodes;
}

bool Type::needsFree() const {
    return m_typeOpCodes.freeResult != nullptr;
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

const EnumType* Type::asEnum() const {
    return nullptr;
}

ValueType::ValueType(NomadString documentation):
    Type(std::move(documentation))
{}

TypeStorage ValueType::getStorage() const {
    return TypeStorage::Value;
}

bool Type::isVoid() const {
    return false;
}

bool Type::isString() const {
    return false;
}

NomadString getTypeDisplayName(const Type* type) {
    if (type == nullptr) {
        return "<unknown>";
    }

    const auto prefix = type->asEnum() == nullptr ? NomadString{} : NomadString{"enum "};
    return prefix + type->getTypeName();
}

///////////////////////////////////////////////////////////////////////////////
// VoidType
void VoidType::initValue(RuntimeValue& value) const {
    value = RuntimeValue{};
}

NomadString VoidType::getTypeName() const {
    return VOID_TYPE_NAME;
}

TypeStorage VoidType::getStorage() const {
    return TypeStorage::None;
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
void IdType::initValue(RuntimeValue& value) const {
    value.setIdValue(0);
}

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
// OpaqueIdType
OpaqueIdType::OpaqueIdType(NomadString name, NomadString documentation):
    ValueType(std::move(documentation)),
    m_name(std::move(name))
{
}

void OpaqueIdType::initValue(RuntimeValue& value) const {
    value.setIdValue(0);
}

NomadString OpaqueIdType::getTypeName() const {
    return m_name;
}

void OpaqueIdType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    destinationValue.setIdValue(sourceValue.getIdValue());
}

void OpaqueIdType::toString(const RuntimeValue& value, NomadString& string) const {
    string = m_name + "(" + nomad::toString(value.getIdValue()) + ")";
}

bool OpaqueIdType::sameType(const Type* other) const {
    return other == this;
}

///////////////////////////////////////////////////////////////////////////////
// FloatType
void FloatType::initValue(RuntimeValue& value) const {
    value.setFloatValue(0.0f);
}

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
void IntegerType::initValue(RuntimeValue& value) const {
    value.setIntegerValue(0);
}

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
// EnumType
EnumType::EnumType(NomadString name, std::vector<EnumValueDefinition> values):
    m_name(std::move(name)),
    m_values(std::move(values))
{
}

void EnumType::initValue(RuntimeValue& value) const {
    value.setIntegerValue(0);
}

NomadString EnumType::getTypeName() const {
    return m_name;
}

void EnumType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    destinationValue.setIntegerValue(sourceValue.getIntegerValue());
}

void EnumType::toString(const RuntimeValue& value, NomadString& string) const {
    if (!getNameByValue(value.getIntegerValue(), string)) {
        string = "<invalid " + m_name + " value>";
    }
}

bool EnumType::sameType(const Type* other) const {
    return other == this;
}

const EnumType* EnumType::asEnum() const {
    return this;
}

NomadIndex EnumType::getValueCount() const {
    return m_values.size();
}

const EnumValueDefinition& EnumType::getValue(const NomadIndex index) const {
    return m_values.at(index);
}

bool EnumType::getValueByName(const NomadString& name, NomadInteger& value) const {
    const auto definition = std::ranges::find(m_values, name, &EnumValueDefinition::name);

    if (definition == m_values.end()) {
        return false;
    }

    value = definition->value;
    return true;
}

bool EnumType::getNameByValue(const NomadInteger value, NomadString& name) const {
    const auto definition = std::ranges::find(m_values, value, &EnumValueDefinition::value);

    if (definition == m_values.end()) {
        return false;
    }

    name = definition->name;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// BooleanType
void BooleanType::initValue(RuntimeValue& value) const {
    value.setBooleanValue(NOMAD_FALSE);
}

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
// ObjectType
ObjectType::~ObjectType() = default;

NomadId ObjectType::registerConstructor(
    Runtime& runtime,
    std::unique_ptr<Constructor> constructor
) {
    if (constructor == nullptr || constructor->getObjectType() != this ||
        constructor->getId() != NOMAD_INVALID_ID || runtime.getTypeByName(getTypeName()) != this) {
        log::error("Cannot register constructor: invalid owner or previously registered callable");
        return NOMAD_INVALID_ID;
    }

    m_constructors.push_back(std::move(constructor));
    const auto id = m_constructors.back()->registerWithRuntime(runtime);
    if (id == NOMAD_INVALID_ID) {
        m_constructors.pop_back();
        log::error("Cannot register constructor: callable registration failed");
    }
    return id;
}

std::vector<const Constructor*> ObjectType::getConstructors() const {
    std::vector<const Constructor*> constructors;
    constructors.reserve(m_constructors.size());
    for (const auto& constructor : m_constructors) {
        constructors.push_back(constructor.get());
    }
    return constructors;
}

ObjectType::ObjectType(
    NomadString name,
    std::vector<const Type*> typeArguments,
    NomadString documentation
):
    Type(std::move(documentation)),
    m_name(std::move(name)),
    m_typeArguments(std::move(typeArguments))
{
    m_typeOpCodes.initResult = op_object_init_r;
    m_typeOpCodes.initIntermediate = op_object_init_i;

    m_typeOpCodes.freeResult = op_object_free_r;
    m_typeOpCodes.freeIntermediate = op_object_free_i;
    m_typeOpCodes.freeStack = op_object_free_stack;
    m_typeOpCodes.freeFunctionVariable = op_object_free_variable;

    m_typeOpCodes.loadFunctionVariable = {
        op_function_variable_object_get_r, op_function_variable_object_get_i, op_function_variable_object_push
    };
    m_typeOpCodes.loadParameter = {
        op_object_parameter_load_r, op_object_parameter_load_i, op_object_parameter_push
    };
    // Dynamic getters hand over an owned object.
    m_typeOpCodes.loadDynamicVariable = {
        op_dynamic_variable_get_r, op_dynamic_variable_get_i, op_dynamic_variable_push
    };
    m_typeOpCodes.loadContextVariable = {
        op_context_variable_object_get_r, op_context_variable_object_get_i, op_context_variable_object_push
    };

    m_typeOpCodes.copyResultToStack = op_object_push_r;
    m_typeOpCodes.moveResultToIntermediate = op_object_move_r_to_i;
    m_typeOpCodes.moveIntermediateToResult = op_object_move_i_to_r;
    m_typeOpCodes.moveResultToStack = op_object_move_r_to_stack;
    m_typeOpCodes.pushIntermediate = op_object_push_i;
    m_typeOpCodes.popResult = op_object_pop_r;
    m_typeOpCodes.popIntermediate = op_object_pop_i;

    // The compiler frees the previous value, then `r` is moved into the variable.
    m_typeOpCodes.setFunctionVariable = op_function_variable_set;
    m_typeOpCodes.setDynamicVariable = op_dynamic_variable_set;
    m_typeOpCodes.setContextVariable = op_context_variable_set;
}

void ObjectType::initValue(RuntimeValue& value) const {
    value.setObjectValue(nullptr);
}

void ObjectType::freeValue(RuntimeValue& value) const {
    value.freeObjectValue();
}

void ObjectType::copyValue(const RuntimeValue& sourceValue, RuntimeValue& destinationValue) const {
    if (&sourceValue == &destinationValue) {
        return;
    }
    RuntimeValue copy;
    copy.copyObjectValue(sourceValue);
    destinationValue.freeObjectValue();
    destinationValue = copy;
}

bool ObjectType::sameType(const Type* other) const {
    return this == other;
}

TypeStorage ObjectType::getStorage() const {
    return TypeStorage::Object;
}

bool ObjectType::isOpen() const {
    return false;
}

NomadId ObjectType::addProperty(
    const NomadString& name,
    const Type* propertyType
) {
    if (propertyType == nullptr) {
        return NOMAD_INVALID_ID;
    }

    const auto existing = std::find_if(
        m_properties.begin(),
        m_properties.end(),
        [&](const ObjectPropertyDefinition& property) {
            return property.name == name;
        }
    );
    if (existing != m_properties.end()) {
        if (!existing->type->sameType(propertyType)) {
            return NOMAD_INVALID_ID;
        }
        return existing->id;
    }

    const auto propertyId = m_properties.size();
    if (propertyId >= toNomadIndex(NOMAD_ID_MAX)) {
        return NOMAD_INVALID_ID;
    }
    const auto id = toNomadId(propertyId);
    m_properties.push_back({id, name, propertyType});
    return id;
}

const ObjectPropertyDefinition* ObjectType::getProperty(const NomadString& name) const {
    const auto property = std::find_if(
        m_properties.begin(),
        m_properties.end(),
        [&](const ObjectPropertyDefinition& candidate) {
            return candidate.name == name;
        }
    );
    return property == m_properties.end() ? nullptr : &*property;
}

const ObjectPropertyDefinition* ObjectType::getProperty(const NomadId id) const {
    return isIdInRange(id, m_properties.size()) ? &m_properties[toNomadIndex(id)] : nullptr;
}

NomadIndex ObjectType::getPropertyCount() const {
    return static_cast<NomadIndex>(m_properties.size());
}

NomadString ObjectType::getTypeName() const {
    return m_name;
}

const std::vector<const Type*>& ObjectType::getTypeArguments() const {
    return m_typeArguments;
}

void ObjectType::toString(const RuntimeValue&, NomadString& string) const {
    string = m_name;
}

///////////////////////////////////////////////////////////////////////////////
// BaseStringType
BaseStringType::BaseStringType():
    ObjectType(STRING_TYPE_NAME)
{
    m_typeOpCodes.initResult = op_string_init_r;
    m_typeOpCodes.initIntermediate = op_string_init_i;
}

void BaseStringType::initValue(RuntimeValue& value) const {
    value.initStringValue();
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
void FunctionLineNumberType::initValue(RuntimeValue& value) const {
    value.setIntegerValue(0);
}

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

void FunctionType::initValue(RuntimeValue& value) const {
    value.setIdValue(0);
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

void EventType::initValue(RuntimeValue& value) const {
    value.setIdValue(0);
}

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
void EventCallbackType::initValue(RuntimeValue& value) const {
    value.setIdValue(0);
}

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
void EventDispatchType::initValue(RuntimeValue& value) const {
    value.setIdValue(0);
}

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
