// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/TypeManager.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/CompositeType.hpp>
#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>

#include <algorithm>
#include <utility>

namespace nomad {

TypeManager::TypeManager(Runtime& runtime):
    m_runtime(runtime)
{}

TypeManager::~TypeManager() = default;

Runtime& TypeManager::getRuntime() const {
    return m_runtime;
}

TypeId TypeManager::reserveTypeId() {
    const auto id = TypeId{toNomadId(m_types.size())};
    m_types.push_back(nullptr);
    return id;
}

void TypeManager::catalogType(std::unique_ptr<Type> type) {
    if (type == nullptr || type->getRuntime() != &m_runtime ||
        isIdOutOfRange(type->getId().id, m_types.size()) ||
        m_types[toNomadIndex(type->getId().id)] != nullptr) {
        throw NomadException("Cannot register a type without a matching runtime and reserved ID");
    }
    m_types[toNomadIndex(type->getId().id)] = std::move(type);
}

TypeId TypeManager::getTypeId(const NomadString& name) const {
    for (auto index = NomadIndex{0}; index < m_types.size(); ++index) {
        if (m_types[index] != nullptr && m_types[index]->getTypeName() == name) {
            return TypeId{toNomadId(index)};
        }
    }
    return TypeId::INVALID;
}

std::optional<const Type*> TypeManager::getType(const TypeId id) const {
    if (isIdOutOfRange(id.id, m_types.size()) || m_types[toNomadIndex(id.id)] == nullptr) {
        return std::nullopt;
    }
    return m_types[toNomadIndex(id.id)].get();
}

const Type* TypeManager::getTypeByName(const NomadString& name) const {
    const auto type = getType(getTypeId(name));
    return type.has_value() ? *type : nullptr;
}

void TypeManager::getTypes(std::vector<const Type*>& types) const {
    types.reserve(types.size() + m_types.size());
    for (const auto& type : m_types) {
        if (type != nullptr) {
            types.push_back(type.get());
        }
    }
}

NomadIndex TypeManager::getTypeCount() const {
    return m_types.size();
}

bool TypeManager::ownsType(const Type* type) const {
    return type != nullptr && type->getRuntime() == &m_runtime && getType(type->getId()) == type;
}

ClassType* TypeManager::getMutableClassType(const ClassType* type) {
    if (!ownsType(type)) {
        return nullptr;
    }
    return static_cast<ClassType*>(m_types[toNomadIndex(type->getId().id)].get());
}

CompositeType* TypeManager::getMutableCompositeType(const TypeId id) {
    const auto type = getType(id);
    if (!type.has_value() || (*type)->asCompositeType() == nullptr) {
        return nullptr;
    }
    return static_cast<CompositeType*>(m_types[toNomadIndex(id.id)].get());
}

void TypeManager::finalizeCompositeFieldLayout(const TypeId id) {
    auto* owner = getMutableCompositeType(id);
    if (owner == nullptr) {
        throw NomadException("Cannot finalize field layout: type ID does not identify a composite type");
    }
    owner->finalizeFieldLayout();
}

PropertyId TypeManager::registerScriptClassProperty(
    const TypeId ownerId,
    const NomadString& name,
    const Type* valueType
) {
    auto* owner = getMutableCompositeType(ownerId);
    return registerScriptProperty(
        owner != nullptr && owner->asClassType() != nullptr ? owner : nullptr, name, valueType
    );
}

PropertyId TypeManager::registerNativeClassProperty(
    const TypeId ownerId,
    NativePropertyDefinition definition
) {
    const auto type = getType(ownerId);
    const auto* classType = type.has_value() ? (*type)->asClassType() : nullptr;
    if (classType == nullptr) {
        log::error("Cannot register native property '" + definition.name + "': owner must identify a class type");
        return PropertyId::INVALID;
    }
    const auto* valueType = definition.valueType;
    const auto hasAccessors = static_cast<bool>(definition.getter) || static_cast<bool>(definition.setter);
    auto property = std::make_unique<NativeProperty>(
        classType, definition.name, valueType,
        std::move(definition.getter), std::move(definition.setter), std::move(definition.documentation)
    );
    const auto deferred = !property->isTypeResolved() &&
        valueType != nullptr && valueType->getRuntime() == &m_runtime;
    if (!deferred && (!ownsType(valueType) || valueType->isVoid() || valueType->isInternal())) {
        log::error("Cannot register native property '" + definition.name + "': invalid value type");
        return PropertyId::INVALID;
    }
    if (!hasAccessors) {
        log::error("Cannot register native property '" + definition.name + "': property has no accessor or mutator");
        return PropertyId::INVALID;
    }
    NomadString error;
    if (!m_runtime.canRegisterNativeMember(classType, definition.name, MemberType::Property, {}, error)) {
        log::error("Cannot register native property '" + definition.name + "': " + error);
        return PropertyId::INVALID;
    }
    auto* owner = getMutableClassType(classType);
    const auto propertyId = owner->registerProperty(m_runtime, std::move(property));
    if (propertyId.isInvalid()) {
        return PropertyId::INVALID;
    }
    m_runtime.catalogNativeProperty(propertyId);
    return propertyId;
}

PropertyId TypeManager::registerScriptStructProperty(
    const TypeId ownerId,
    const NomadString& name,
    const Type* valueType
) {
    auto* owner = getMutableCompositeType(ownerId);
    return registerScriptProperty(
        owner != nullptr && owner->asStructType() != nullptr ? owner : nullptr, name, valueType
    );
}

PropertyId TypeManager::registerScriptProperty(
    CompositeType* owner,
    const NomadString& name,
    const Type* valueType
) {
    if (owner == nullptr || !ownsType(valueType) ||
        valueType->isVoid() || valueType->isInternal()) {
        log::error("Cannot register property '" + name + "': invalid owner or value type");
        return PropertyId::INVALID;
    }
    if (const auto* existing = owner->getProperty(name)) {
        if (existing->asNativeProperty() == nullptr && existing->getValueType() == valueType) {
            return existing->getId();
        }
        log::error("Cannot register property '" + name + "': conflicting definition");
        return PropertyId::INVALID;
    }
    if (m_runtime.getMemberType(m_runtime.getNativeMemberId(owner, name)) != MemberType::Unknown) {
        log::error("Cannot register property '" + name + "': the type already has a member with that name");
        return PropertyId::INVALID;
    }
    return owner->registerProperty(m_runtime, std::make_unique<Property>(owner, name, valueType));
}

CallableId TypeManager::registerScriptClassMethod(
    const TypeId ownerId,
    NomadString name,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
    const auto type = getType(ownerId);
    auto* owner = type && (*type)->asClassType() != nullptr ? getMutableClassType((*type)->asClassType()) : nullptr;
    if (owner == nullptr || name.empty() ||
        std::ranges::any_of(parameters, [this](const auto& parameter) {
            return parameter.name == "this" || !ownsType(parameter.type) ||
                parameter.type->isVoid() || parameter.type->isInternal();
        })) {
        log::error("Cannot register script method: invalid owner, name, or parameter type");
        return CallableId::INVALID;
    }
    for (const auto* method : owner->getMethods()) {
        if (method->getName() == name && std::ranges::equal(method->getParameters(), parameters,
            [](const auto& first, const auto& second) { return first.type == second.type; })) {
            log::error("Cannot register script method: duplicate signature");
            return CallableId::INVALID;
        }
    }
    auto method = std::make_unique<ScriptMethod>(
        std::move(name), owner, std::move(parameters), nullptr, std::move(path), std::move(source)
    );
    return owner->registerMethod(m_runtime, std::move(method));
}

CallableId TypeManager::registerScriptClassConstructor(
    const TypeId ownerId,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
    const auto type = getType(ownerId);
    auto* owner = type && (*type)->asClassType() != nullptr ? getMutableClassType((*type)->asClassType()) : nullptr;
    if (owner == nullptr ||
        std::ranges::any_of(parameters, [this](const auto& parameter) {
            return parameter.name == "this" || !ownsType(parameter.type) ||
                parameter.type->isVoid() || parameter.type->isInternal();
        })) {
        log::error("Cannot register script constructor: invalid owner or parameter type");
        return CallableId::INVALID;
    }
    for (const auto* constructor : owner->getConstructors()) {
        if (std::ranges::equal(constructor->getParameters(), parameters,
            [](const auto& first, const auto& second) { return first.type == second.type; })) {
            log::error("Cannot register script constructor: duplicate signature");
            return CallableId::INVALID;
        }
    }
    auto constructor = std::make_unique<ScriptConstructor>(
        owner, std::move(parameters), std::move(path), std::move(source)
    );
    return owner->registerConstructor(m_runtime, std::move(constructor));
}

CallableId TypeManager::registerNativeClassMethod(
    const TypeId ownerId,
    NativeMethodDefinition definition
) {
    const auto type = getType(ownerId);
    const auto* classType = type.has_value() ? (*type)->asClassType() : nullptr;
    if (classType == nullptr) {
        log::error("Cannot register native method '" + definition.name + "': owner must identify a class type");
        return CallableId::INVALID;
    }
    const auto returnType = getType(definition.returnTypeId);
    if (!returnType.has_value()) {
        log::error("Cannot register native method '" + definition.name + "': invalid return type ID");
        return CallableId::INVALID;
    }
    std::vector<Parameter> parameters;
    parameters.reserve(definition.parameters.size());
    for (const auto& parameterDefinition : definition.parameters) {
        const auto parameterType = getType(parameterDefinition.typeId);
        if (!parameterType.has_value()) {
            log::error("Cannot register native method '" + definition.name + "': invalid parameter type ID");
            return CallableId::INVALID;
        }
        parameters.push_back(defParameter(
            parameterDefinition.name, *parameterType, parameterDefinition.documentation
        ));
    }
    if (!definition.function || ((*returnType)->isInternal() && !(*returnType)->isVoid())) {
        log::error("Cannot register native callable '" + definition.name + "': invalid callback or return type");
        return CallableId::INVALID;
    }
    for (const auto& parameter : parameters) {
        if (parameter.name.empty() || parameter.type->isVoid()) {
            log::error("Cannot register native callable '" + definition.name + "': invalid parameter definition");
            return CallableId::INVALID;
        }
    }
    NomadString error;
    if (!m_runtime.canRegisterNativeMember(classType, definition.name, MemberType::Method, parameters, error)) {
        log::error("Cannot register native method '" + definition.name + "': " + error);
        return CallableId::INVALID;
    }
    auto* owner = getMutableClassType(classType);
    const auto id = owner->registerMethod(m_runtime, std::make_unique<NativeMethod>(
        definition.name, classType, std::move(definition.function), std::move(parameters),
        *returnType, std::move(definition.documentation)
    ));
    if (id.isInvalid()) {
        return CallableId::INVALID;
    }
    m_runtime.catalogNativeMethod(id);
    return id;
}

CallableId TypeManager::registerNativeClassConstructor(
    const TypeId ownerId,
    NativeConstructorDefinition definition
) {
    return registerNativeClassConstructor(ownerId, ownerId, std::move(definition));
}

CallableId TypeManager::registerNativeClassConstructor(
    const TypeId ownerId,
    const TypeId createdTypeId,
    NativeConstructorDefinition definition
) {
    const auto type = getType(ownerId);
    const auto* classType = type.has_value() ? (*type)->asClassType() : nullptr;
    if (classType == nullptr) {
        log::error("Cannot register constructor: owner must identify a class type");
        return CallableId::INVALID;
    }
    const auto resultType = getType(createdTypeId);
    const auto* createdType = resultType.has_value() ? (*resultType)->asClassType() : nullptr;
    if (createdType == nullptr) {
        log::error("Cannot register constructor: created type must identify a class type");
        return CallableId::INVALID;
    }
    std::vector<Parameter> parameters;
    parameters.reserve(definition.parameters.size());
    for (const auto& parameterDefinition : definition.parameters) {
        const auto parameterType = getType(parameterDefinition.typeId);
        if (!parameterType.has_value()) {
            log::error("Cannot register constructor: invalid parameter type ID");
            return CallableId::INVALID;
        }
        parameters.push_back(defParameter(
            parameterDefinition.name, *parameterType, parameterDefinition.documentation
        ));
    }
    auto* owner = getMutableClassType(classType);
    if (createdType->isInternal() || !definition.function) {
        log::error("Cannot register constructor: a registered class, registered public class type, and callback are required");
        return CallableId::INVALID;
    }
    const auto name = classType->getTypeName();
    if (name.empty() || m_runtime.getParameterizedConstructorArgumentCount(name) ||
        m_runtime.getParameterizedTypeArgumentCount(name)) {
        log::error("Cannot register constructor '" + name + "': the name is reserved");
        return CallableId::INVALID;
    }
    for (const auto& parameter : parameters) {
        if (parameter.type->isVoid()) {
            log::error("Cannot register constructor: parameter types must be registered and non-void");
            return CallableId::INVALID;
        }
    }
    for (const auto* existing : m_runtime.getConstructors(classType)) {
        if (existing->getParameterCount() != parameters.size()) {
            continue;
        }
        if (std::ranges::equal(existing->getParameters(), parameters,
            [](const auto& left, const auto& right) { return left.type->sameType(right.type); })) {
            log::error("Cannot register constructor '" + name + "': duplicate signature");
            return CallableId::INVALID;
        }
    }
    return owner->registerConstructor(m_runtime, std::make_unique<NativeConstructor>(
        name, classType, createdType, std::move(definition.function), std::move(parameters),
        std::move(definition.documentation)
    ));
}

} // namespace nomad
