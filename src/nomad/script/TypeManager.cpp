// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/TypeManager.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/CompositeType.hpp>
#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/StructType.hpp>

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

void TypeManager::registerType(std::unique_ptr<Type> type) {
    const auto belongsToRuntime = type != nullptr && type->getRuntime() == &m_runtime;
    const auto hasReservedId = belongsToRuntime && !isIdOutOfRange(type->getId().id, m_types.size());
    const auto hasUnusedId = hasReservedId && !getType(type->getId()).has_value();
    if (!hasUnusedId) {
        throw NomadException("Cannot register a type without a matching runtime and reserved ID");
    }
    m_runtime.publishTypeNamespace(*type);
    catalogType(std::move(type));
}

TypeId TypeManager::registerClass(
    NomadString name,
    const ClassMode mode,
    NomadString documentation
) {
    return registerClass<ClassType>(
        std::move(name), mode, std::vector<const Type*>{}, std::move(documentation)
    );
}

bool TypeManager::canRegisterClass(const NomadString& name, const ClassMode mode) const {
    if (name.empty()) {
        log::error("Cannot register a class with an empty name");
        return false;
    }
    if (mode != ClassMode::Opened && mode != ClassMode::Closed) {
        log::error("Cannot register class '" + name + "': invalid class mode");
        return false;
    }
    if (getTypeId(name).isValid()) {
        log::error("Type with name '" + name + "' already registered");
        return false;
    }
    if (!m_runtime.canRegisterTypeName(name)) {
        log::error("Cannot register class '" + name + "': name is reserved by a hub declaration");
        return false;
    }
    return true;
}

TypeId TypeManager::publishClass(
    const TypeId id,
    const NomadString& name,
    const ClassMode mode,
    std::unique_ptr<ClassType> type
) {
    const auto matchesIdentity = type->getRuntime() == &m_runtime &&
        type->getId() == id && type->getTypeName() == name;
    // Descriptor construction may register other types, so recheck name availability.
    const auto canPublish = matchesIdentity && canRegisterClass(name, mode);
    const auto matchesMode = canPublish && type->isOpen() == (mode == ClassMode::Opened);
    if (!canPublish || !matchesMode) {
        log::error("Cannot register class '" + name + "': descriptor does not match its registration request");
        return TypeId::INVALID;
    }
    registerType(std::move(type));
    return id;
}

TypeId TypeManager::registerStruct(NomadString name, NomadString documentation) {
    const auto hasAvailableName = !name.empty() && getTypeId(name).isInvalid();
    const auto canRegisterName = hasAvailableName && m_runtime.canRegisterTypeName(name);
    if (!canRegisterName) {
        log::error("Cannot register struct '" + name + "': invalid or reserved type name");
        return TypeId::INVALID;
    }
    const auto id = reserveTypeId();
    registerType(std::make_unique<StructType>(&m_runtime, id, std::move(name), std::move(documentation)));
    return id;
}

TypeId TypeManager::reserveTypeId() {
    const auto id = TypeId{toNomadId(m_types.size())};
    m_types.push_back(nullptr);
    return id;
}

void TypeManager::catalogType(std::unique_ptr<Type> type) {
    const auto belongsToRuntime = type != nullptr && type->getRuntime() == &m_runtime;
    const auto hasReservedId = belongsToRuntime && !isIdOutOfRange(type->getId().id, m_types.size());
    const auto hasUnusedId = hasReservedId && m_types[toNomadIndex(type->getId().id)] == nullptr;
    if (!hasUnusedId) {
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
    if (isIdOutOfRange(id.id, m_types.size())) {
        return std::nullopt;
    }
    const auto* type = m_types[toNomadIndex(id.id)].get();
    if (type == nullptr) {
        return std::nullopt;
    }
    return type;
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
    const auto belongsToRuntime = type != nullptr && type->getRuntime() == &m_runtime;
    if (!belongsToRuntime) {
        return false;
    }
    // Matching IDs alone do not establish ownership of the descriptor.
    const auto registeredType = getType(type->getId());
    return registeredType == type;
}

ClassType* TypeManager::getMutableClassType(const ClassType* type) {
    if (!ownsType(type)) {
        return nullptr;
    }
    return static_cast<ClassType*>(m_types[toNomadIndex(type->getId().id)].get());
}

CompositeType* TypeManager::getMutableCompositeType(const TypeId id) {
    const auto type = getType(id);
    const auto* compositeType = type.has_value() ? (*type)->asCompositeType() : nullptr;
    if (compositeType == nullptr) {
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
    const auto isClassOwner = owner != nullptr && owner->asClassType() != nullptr;
    auto* classOwner = isClassOwner ? owner : nullptr;
    return registerScriptProperty(classOwner, name, valueType);
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
    const auto belongsToRuntime = valueType != nullptr && valueType->getRuntime() == &m_runtime;
    const auto isDeferredType = !property->isTypeResolved() && belongsToRuntime;
    // Same-runtime placeholders may be registered before their value type resolves.
    const auto hasValidValueType = isDeferredType ||
        (ownsType(valueType) && !valueType->isVoid() && !valueType->isInternal());
    if (!hasValidValueType) {
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
    const auto isStructOwner = owner != nullptr && owner->asStructType() != nullptr;
    auto* structOwner = isStructOwner ? owner : nullptr;
    return registerScriptProperty(structOwner, name, valueType);
}

PropertyId TypeManager::registerScriptProperty(
    CompositeType* owner,
    const NomadString& name,
    const Type* valueType
) {
    const auto hasValidValueType = owner != nullptr && ownsType(valueType) &&
        !valueType->isVoid() && !valueType->isInternal();
    if (owner == nullptr || !hasValidValueType) {
        log::error("Cannot register property '" + name + "': invalid owner or value type");
        return PropertyId::INVALID;
    }
    if (const auto* existing = owner->getProperty(name)) {
        const auto isMatchingStoredProperty = existing->asNativeProperty() == nullptr &&
            existing->getValueType() == valueType;
        if (isMatchingStoredProperty) {
            return existing->getId();
        }
        log::error("Cannot register property '" + name + "': conflicting definition");
        return PropertyId::INVALID;
    }
    const auto nativeMember = m_runtime.getNativeMemberId(owner, name);
    const auto hasNativeMember = m_runtime.getMemberType(nativeMember) != MemberType::Unknown;
    if (hasNativeMember) {
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
    const auto* classType = type.has_value() ? (*type)->asClassType() : nullptr;
    auto* owner = getMutableClassType(classType);
    const auto canValidateParameters = owner != nullptr && !name.empty();
    const auto hasInvalidParameters = canValidateParameters && std::ranges::any_of(parameters, [this](const auto& parameter) {
        const auto isReceiverName = parameter.name == "this";
        const auto hasValidType = !isReceiverName && ownsType(parameter.type) &&
            !parameter.type->isVoid() && !parameter.type->isInternal();
        return !hasValidType;
    });
    if (owner == nullptr || name.empty() || hasInvalidParameters) {
        log::error("Cannot register script method: invalid owner, name, or parameter type");
        return CallableId::INVALID;
    }
    for (const auto* method : owner->getMethods()) {
        const auto hasSameName = method->getName() == name;
        const auto hasSameSignature = hasSameName && std::ranges::equal(
            method->getParameters(), parameters,
            [](const auto& first, const auto& second) { return first.type == second.type; }
        );
        if (hasSameSignature) {
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
    const auto* classType = type.has_value() ? (*type)->asClassType() : nullptr;
    auto* owner = getMutableClassType(classType);
    const auto hasInvalidParameters = owner != nullptr && std::ranges::any_of(parameters, [this](const auto& parameter) {
        const auto isReceiverName = parameter.name == "this";
        const auto hasValidType = !isReceiverName && ownsType(parameter.type) &&
            !parameter.type->isVoid() && !parameter.type->isInternal();
        return !hasValidType;
    });
    if (owner == nullptr || hasInvalidParameters) {
        log::error("Cannot register script constructor: invalid owner or parameter type");
        return CallableId::INVALID;
    }
    for (const auto* constructor : owner->getConstructors()) {
        const auto hasSameSignature = std::ranges::equal(
            constructor->getParameters(), parameters,
            [](const auto& first, const auto& second) { return first.type == second.type; }
        );
        if (hasSameSignature) {
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
    const auto hasValidReturnType = definition.function &&
        (!(*returnType)->isInternal() || (*returnType)->isVoid());
    if (!definition.function || !hasValidReturnType) {
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
    const auto isReservedName = name.empty() ||
        m_runtime.getParameterizedConstructorArgumentCount(name).has_value() ||
        m_runtime.getParameterizedTypeArgumentCount(name).has_value();
    if (isReservedName) {
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
        const auto hasSameSignature = std::ranges::equal(
            existing->getParameters(), parameters,
            [](const auto& left, const auto& right) { return left.type->sameType(right.type); }
        );
        if (hasSameSignature) {
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
