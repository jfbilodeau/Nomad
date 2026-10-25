// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Type.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/Event.hpp>
#include <nomad/script/Runtime.hpp>

#include <nomad/system/String.hpp>

#include <algorithm>

namespace nomad {

///////////////////////////////////////////////////////////////////////////////
// Type
Type::Type(Runtime* runtime, const TypeId id, NomadString documentation):
    m_runtime(runtime),
    m_id(id),
    m_documentation(std::move(documentation))
{
    if (runtime == nullptr) {
        throw NomadException("A type requires an owning runtime");
    }
}

Runtime* Type::getRuntime() const {
    return m_runtime;
}

TypeId Type::getId() const {
    return m_id;
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

const ObjectType* Type::asObjectType() const {
    return nullptr;
}

ValueType::ValueType(Runtime* runtime, TypeId id, NomadString documentation):
    Type(runtime, id, std::move(documentation))
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
OpaqueIdType::OpaqueIdType(Runtime* runtime, TypeId id, NomadString name, NomadString documentation):
    ValueType(runtime, id, std::move(documentation)),
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
EnumType::EnumType(
    Runtime* runtime,
    TypeId id,
    NomadString name,
    std::vector<EnumValueDefinition> values,
    NomadString documentation
):
    ValueType(runtime, id, std::move(documentation)),
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

CallableId ObjectType::registerConstructor(
    Runtime& runtime,
    std::unique_ptr<Constructor> constructor
) {
    if (constructor == nullptr || constructor->getObjectType() != this ||
        constructor->getId() != CallableId{} || runtime.getType(getId()) != this) {
        log::error("Cannot register constructor: invalid owner or previously registered callable");
        return CallableId::INVALID;
    }

    m_constructors.push_back(std::move(constructor));
    const auto id = m_constructors.back()->registerWithRuntime(runtime);
    if (id == CallableId{}) {
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

CallableId ObjectType::registerMethod(
    Runtime& runtime,
    std::unique_ptr<Method> method
) {
    if (method == nullptr || method->getObjectType() != this ||
        method->getId() != CallableId{} || runtime.getType(getId()) != this ||
        getProperty(method->getName()) != nullptr) {
        log::error("Cannot register method: invalid owner or previously registered callable");
        return CallableId::INVALID;
    }

    m_methods.push_back(std::move(method));
    const auto id = m_methods.back()->registerWithRuntime(runtime);
    if (id == CallableId{}) {
        m_methods.pop_back();
        log::error("Cannot register method: callable registration failed");
    }
    return id;
}

std::vector<const Method*> ObjectType::getMethods() const {
    std::vector<const Method*> methods;
    methods.reserve(m_methods.size());
    for (const auto& method : m_methods) {
        methods.push_back(method.get());
    }
    return methods;
}

ObjectType::ObjectType(
    Runtime* runtime,
    TypeId id,
    NomadString name,
    const ObjectTypeMode mode,
    std::vector<const Type*> typeArguments,
    NomadString documentation
):
    Type(runtime, id, std::move(documentation)),
    m_name(std::move(name)),
    m_mode(mode),
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

const ObjectType* ObjectType::asObjectType() const {
    return this;
}

bool ObjectType::isOpen() const {
    return m_mode == ObjectTypeMode::Opened;
}

PropertyId ObjectType::registerProperty(
    Runtime& runtime,
    std::unique_ptr<Property> property
) {
    if (property == nullptr || property->getObjectType() != this || property->getId().isValid() ||
        runtime.getType(getId()) != this ||
        property->getValueType() == nullptr || property->getValueType()->isVoid() ||
        property->getValueType()->isInternal() ||
        runtime.getType(property->getValueType()->getId()) != property->getValueType() ||
        getProperty(property->getName()) != nullptr ||
        std::ranges::any_of(m_methods, [&](const auto& method) { return method->getName() == property->getName(); })) {
        log::error("Cannot register property: invalid owner, value type, or duplicate definition");
        return PropertyId::INVALID;
    }
    m_properties.push_back(std::move(property));
    const auto id = m_properties.back()->registerWithRuntime(runtime);
    if (id.isInvalid()) {
        m_properties.pop_back();
        log::error("Cannot register property: property registration failed");
    }
    return id;
}

const Property* ObjectType::getProperty(const NomadString& name) const {
    const auto property = std::find_if(
        m_properties.begin(),
        m_properties.end(),
        [&](const auto& candidate) {
            return candidate->getName() == name;
        }
    );
    return property == m_properties.end() ? nullptr : property->get();
}

const Property* ObjectType::getProperty(const PropertyId id) const {
    const auto property = std::find_if(m_properties.begin(), m_properties.end(), [id](const auto& candidate) {
        return candidate->getId() == id;
    });
    return property == m_properties.end() ? nullptr : property->get();
}

std::vector<const Property*> ObjectType::getProperties() const {
    std::vector<const Property*> properties;
    properties.reserve(m_properties.size());
    for (const auto& property : m_properties) {
        properties.push_back(property.get());
    }
    return properties;
}

NomadIndex ObjectType::getPropertyCount() const {
    return static_cast<NomadIndex>(m_properties.size());
}

NomadIndex ObjectType::getFieldIndex(const PropertyId id) const {
    auto index = NomadIndex{0};
    for (const auto& property : m_properties) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        if (property->getId() == id) {
            return index;
        }
        ++index;
    }
    return NOMAD_INVALID_INDEX;
}

IndexerId ObjectType::registerNativeIndexer(
    Runtime& runtime,
    NativeIndexerInfo info
) {
    if (info.objectType != this || info.id.isValid() ||
        runtime.getType(getId()) != this) {
        log::error("Cannot register native indexer: invalid owner or identity");
        return IndexerId::INVALID;
    }
    const auto isValidType = [&](const Type* type) {
        return type != nullptr && !type->isVoid() && !type->isInternal() &&
            runtime.getType(type->getId()) == type;
    };
    if (!isValidType(info.valueType)) {
        log::error("Cannot register native indexer: value type must be registered, public, and non-void");
        return IndexerId::INVALID;
    }
    if (info.indexTypes.empty()) {
        log::error("Cannot register native indexer: at least one index type is required");
        return IndexerId::INVALID;
    }
    if (!std::ranges::all_of(info.indexTypes, isValidType)) {
        log::error("Cannot register native indexer: index types must be registered, public, and non-void");
        return IndexerId::INVALID;
    }
    if (!info.getFn && !info.setFn) {
        log::error("Cannot register native indexer: indexer has no accessor or mutator");
        return IndexerId::INVALID;
    }
    if (getNativeIndexer(info.indexTypes.size()) != nullptr) {
        log::error("Cannot register native indexer: type already has an indexer with " +
            nomad::toString(info.indexTypes.size()) + " indices");
        return IndexerId::INVALID;
    }
    m_nativeIndexers.push_back(std::make_unique<NativeIndexerInfo>(std::move(info)));
    auto& indexer = *m_nativeIndexers.back();
    const auto id = runtime.catalogNativeIndexer(&indexer);
    if (id.isInvalid()) {
        m_nativeIndexers.pop_back();
        log::error("Cannot register native indexer: indexer registration failed");
        return IndexerId::INVALID;
    }
    indexer.id = id;
    return id;
}

const NativeIndexerInfo* ObjectType::getNativeIndexer(const NomadIndex indexCount) const {
    const auto indexer = std::find_if(m_nativeIndexers.begin(), m_nativeIndexers.end(), [&](const auto& candidate) {
        return candidate->indexTypes.size() == indexCount;
    });
    return indexer == m_nativeIndexers.end() ? nullptr : indexer->get();
}

std::vector<const NativeIndexerInfo*> ObjectType::getNativeIndexers() const {
    std::vector<const NativeIndexerInfo*> indexers;
    indexers.reserve(m_nativeIndexers.size());
    for (const auto& indexer : m_nativeIndexers) {
        indexers.push_back(indexer.get());
    }
    return indexers;
}

void ObjectType::registerIterableType(
    Runtime& runtime,
    const Type* valueType,
    const Method* factory
) {
    const auto* iteratorType = runtime.getIteratorType(valueType);
    if (runtime.getType(getId()) != this ||
        factory == nullptr || runtime.getMethod(factory->getId()) != factory ||
        factory->getObjectType() != this || factory->getParameterCount() != 0 ||
        factory->getReturnType() != iteratorType || m_iterableInfo.has_value()) {
        throw NomadException("Invalid iterable registration");
    }
    m_iterableInfo = IterableInfo{valueType, iteratorType, factory->getId()};
}

std::optional<IterableInfo> ObjectType::getIterableInfo() const {
    return m_iterableInfo;
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
BaseStringType::BaseStringType(Runtime* runtime, TypeId id):
    ObjectType(runtime, id, STRING_TYPE_NAME, ObjectTypeMode::Closed)
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
EventType::EventType(Runtime* runtime, TypeId id, const EventDefinition* declaration):
    ValueType(runtime, id),
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
