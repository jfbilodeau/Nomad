// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/TypeManager.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/CompositeType.hpp>
#include <nomad/script/EnumType.hpp>
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
    const auto isClassOwner = owner != nullptr && owner->isClass();
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
    const auto hasCallbacks = static_cast<bool>(definition.getter) || static_cast<bool>(definition.setter);
    const auto hasMethods = definition.getterMethodId.isValid() || definition.setterMethodId.isValid();
    if (hasMethods && hasCallbacks) {
        log::error("Cannot register native property '" + definition.name + "': callbacks and accessor methods cannot be mixed");
        return PropertyId::INVALID;
    }
    if (hasMethods) {
        const auto* getter = m_runtime.getMethod(definition.getterMethodId);
        const auto* setter = m_runtime.getMethod(definition.setterMethodId);
        if (!ownsType(valueType) || valueType->isVoid() || valueType->isInternal()) {
            log::error("Cannot register native property '" + definition.name + "': accessor methods require a resolved value type");
            return PropertyId::INVALID;
        }
        if (definition.getterMethodId.isValid() &&
            (getter == nullptr || getter->asNativeMethod() == nullptr || getter->getClassType() != classType ||
             getter->getParameterCount() != 0 || getter->getReturnType() != valueType)) {
            log::error("Cannot register native property '" + definition.name + "': getter must be a native method on its owner with no arguments and a matching result");
            return PropertyId::INVALID;
        }
        if (definition.setterMethodId.isValid() &&
            (setter == nullptr || setter->asNativeMethod() == nullptr || setter->getClassType() != classType ||
             setter->getParameterCount() != 1 || setter->getParameters()[0].type != valueType ||
             setter->getReturnType() == nullptr || !setter->getReturnType()->isVoid())) {
            log::error("Cannot register native property '" + definition.name + "': setter must be a native method on its owner with one matching argument and a void result");
            return PropertyId::INVALID;
        }
    }
    const auto hasAccessors = hasCallbacks || hasMethods;
    auto property = std::make_unique<NativeProperty>(
        classType, definition.name, valueType,
        std::move(definition.getter), std::move(definition.setter), std::move(definition.documentation),
        definition.getterMethodId, definition.setterMethodId
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
    const auto isStructOwner = owner != nullptr && owner->isStruct();
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
    return registerScriptMethod(owner, std::move(name), std::move(parameters), std::move(path), std::move(source));
}

CallableId TypeManager::registerScriptStructMethod(
    const TypeId ownerId,
    NomadString name,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
    auto* owner = getMutableCompositeType(ownerId);
    return registerScriptMethod(owner != nullptr && owner->isStruct() ? owner : nullptr,
        std::move(name), std::move(parameters), std::move(path), std::move(source));
}

CallableId TypeManager::registerScriptMethod(
    CompositeType* owner,
    NomadString name,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
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
    return registerScriptConstructor(owner, std::move(parameters), std::move(path), std::move(source));
}

CallableId TypeManager::registerScriptStructConstructor(
    const TypeId ownerId,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
    const auto type = getType(ownerId);
    auto* owner = type.has_value() && (*type)->isStruct() ? getMutableCompositeType(ownerId) : nullptr;
    return registerScriptConstructor(owner, std::move(parameters), std::move(path), std::move(source));
}

CallableId TypeManager::registerScriptConstructor(
    CompositeType* owner,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
) {
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
    return registerNativeMethod(getMutableClassType(classType), std::move(definition));
}

CallableId TypeManager::registerNativeStructMethod(
    const TypeId ownerId,
    NativeMethodDefinition definition
) {
    auto* owner = getMutableCompositeType(ownerId);
    return registerNativeMethod(owner != nullptr && owner->isStruct() ? owner : nullptr,
        std::move(definition));
}

CallableId TypeManager::registerNativeMethod(
    CompositeType* owner,
    NativeMethodDefinition definition
) {
    if (owner == nullptr) {
        log::error("Cannot register native method '" + definition.name + "': invalid owner type");
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
    if (!m_runtime.canRegisterNativeMember(owner, definition.name, MemberType::Method, parameters, error)) {
        log::error("Cannot register native method '" + definition.name + "': " + error);
        return CallableId::INVALID;
    }
    const auto id = owner->registerMethod(m_runtime, std::make_unique<NativeMethod>(
        definition.name, owner, std::move(definition.function), std::move(parameters),
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
    return registerNativeConstructor(getMutableClassType(classType), createdType, std::move(definition));
}

CallableId TypeManager::registerNativeStructConstructor(
    const TypeId ownerId,
    NativeConstructorDefinition definition
) {
    const auto type = getType(ownerId);
    auto* owner = type.has_value() && (*type)->isStruct() ? getMutableCompositeType(ownerId) : nullptr;
    if (owner == nullptr) {
        log::error("Cannot register constructor: owner must identify a struct type");
        return CallableId::INVALID;
    }
    return registerNativeConstructor(owner, owner, std::move(definition));
}

CallableId TypeManager::registerNativeConstructor(
    CompositeType* owner,
    const Type* createdType,
    NativeConstructorDefinition definition
) {
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
    if (createdType->isInternal() || !definition.function) {
        log::error("Cannot register constructor: a registered public constructed type and callback are required");
        return CallableId::INVALID;
    }
    const auto name = owner->getTypeName();
    const auto isReservedName = name.empty() ||
        getParameterizedConstructorArgumentCount(name).has_value() ||
        getParameterizedTypeArgumentCount(name).has_value();
    if (isReservedName) {
        log::error("Cannot register constructor '" + name + "': the name is reserved");
        return CallableId::INVALID;
    }
    for (const auto& parameter : parameters) {
        if (owner->isStruct() && (parameter.type->isInternal() || parameter.name == "this")) {
            log::error("Cannot register struct constructor: invalid parameter type or receiver parameter");
            return CallableId::INVALID;
        }
        if (parameter.type->isVoid()) {
            log::error("Cannot register constructor: parameter types must be registered and non-void");
            return CallableId::INVALID;
        }
    }
    for (const auto* existing : owner->getConstructors()) {
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
        name, owner, createdType, std::move(definition.function), std::move(parameters),
        std::move(definition.documentation)
    ));
}

const ClassType* TypeManager::getIteratorType(const Type* valueType) {
    const auto hasValidValueType = ownsType(valueType) && !valueType->isVoid() && !valueType->isInternal();
    if (!hasValidValueType) {
        throw NomadException("Iterator value type must be a registered non-void type");
    }
    const auto* type = getParameterizedType("Iterator", {valueType});
    if (type == nullptr) {
        throw NomadException("Failed to specialize iterator type");
    }
    return static_cast<const ClassType*>(type);
}

void TypeManager::registerNativeClassIterable(const TypeId ownerId, NativeInterableDefinition definition) {
    const auto type = getType(ownerId);
    auto* owner = type.has_value() ? getMutableClassType((*type)->asClassType()) : nullptr;
    if (owner == nullptr) {
        throw NomadException("Cannot register iterable: owner must identify a class type");
    }
    const auto valueType = getType(definition.valueTypeId);
    const auto hasValidValueType = valueType.has_value() && !(*valueType)->isVoid() && !(*valueType)->isInternal();
    if (!hasValidValueType) {
        throw NomadException("Cannot register iterable: value type must identify a public, non-void type");
    }
    if (owner->getIterableInfo().has_value()) {
        throw NomadException("Cannot register iterable: type already has an iterable association");
    }
    const auto hasValidFactory = definition.factory.parameters.empty() && definition.factory.function;
    if (!hasValidFactory) {
        throw NomadException("Cannot register iterable: a parameterless factory callback is required");
    }
    const auto returnType = getType(definition.factory.returnTypeId);
    const auto hasMatchingReturnType = returnType.has_value() && *returnType == getIteratorType(*valueType);
    if (!hasMatchingReturnType) {
        throw NomadException("Cannot register iterable: factory must return the matching iterator type");
    }
    const auto factoryId = registerNativeClassMethod(ownerId, std::move(definition.factory));
    if (factoryId.isInvalid()) {
        throw NomadException("Cannot register iterable: failed to register factory method");
    }
    owner->registerIterableType(m_runtime, *valueType, m_runtime.getMethod(factoryId));
}

std::optional<IterableInfo> TypeManager::getIterableInfo(const Type* sourceType) const {
    if (!ownsType(sourceType)) {
        return std::nullopt;
    }
    const auto* owner = sourceType->asClassType();
    return owner == nullptr ? std::nullopt : owner->getIterableInfo();
}

IndexerId TypeManager::registerNativeClassIndexer(const TypeId ownerId, NativeIndexerDefinition definition) {
    const auto type = getType(ownerId);
    const auto* classType = type.has_value() ? (*type)->asClassType() : nullptr;
    if (classType == nullptr) {
        log::error("Cannot register native indexer: owner must identify a class type");
        return IndexerId::INVALID;
    }
    const auto valueType = getType(definition.valueTypeId);
    if (!valueType.has_value()) {
        log::error("Cannot register native indexer: invalid value type ID");
        return IndexerId::INVALID;
    }
    std::vector<const Type*> indexTypes;
    indexTypes.reserve(definition.indexTypes.size());
    for (const auto indexTypeId : definition.indexTypes) {
        const auto indexType = getType(indexTypeId);
        if (!indexType.has_value()) {
            log::error("Cannot register native indexer: invalid index type ID");
            return IndexerId::INVALID;
        }
        indexTypes.push_back(*indexType);
    }
    auto* owner = getMutableClassType(classType);
    if (owner == nullptr) {
        log::error("Cannot register native indexer: class type must be registered with this runtime");
        return IndexerId::INVALID;
    }
    return owner->registerNativeIndexer(m_runtime, {
        IndexerId::INVALID, classType, std::move(indexTypes), *valueType,
        std::move(definition.getter), std::move(definition.setter), definition.documentation
    });
}

bool TypeManager::hasConstructor(const NomadString& name) const {
    for (const auto& type : m_types) {
        const auto* owner = type != nullptr ? type->asCompositeType() : nullptr;
        if (owner == nullptr) {
            continue;
        }
        const auto constructors = owner->getConstructors();
        const auto matchesName = std::ranges::any_of(constructors, [&](const auto* constructor) {
            return constructor->getName() == name;
        });
        if (matchesName) {
            return true;
        }
    }
    return false;
}

bool TypeManager::registerNativeParameterizedConstructor(
    const NomadString& name,
    const NomadIndex argumentCount,
    ParameterizedConstructorInitializer initializer
) {
    const auto hasAvailableName = !name.empty() && m_runtime.canRegisterTypeName(name) &&
        getTypeByName(name) == nullptr;
    const auto isConstructorFamily = getParameterizedConstructorArgumentCount(name).has_value();
    const auto isTypeFamily = getParameterizedTypeArgumentCount(name).has_value();
    const auto hasExistingConstructor = hasConstructor(name);
    if (!hasAvailableName || argumentCount == 0 || !initializer ||
        isConstructorFamily || isTypeFamily || hasExistingConstructor) {
        log::error("Cannot register parameterized constructor '" + name + "': invalid or duplicate definition");
        return false;
    }
    m_parameterizedConstructors.push_back({name, argumentCount, std::move(initializer)});
    m_runtime.publishTypeFamilyNamespace(name, argumentCount);
    return true;
}

std::optional<NomadIndex> TypeManager::getParameterizedConstructorArgumentCount(const NomadString& name) const {
    const auto registration = std::ranges::find(
        m_parameterizedConstructors, name, &ParameterizedConstructorRegistration::name
    );
    if (registration == m_parameterizedConstructors.end()) {
        return std::nullopt;
    }
    return registration->argumentCount;
}

void TypeManager::getParameterizedConstructorNames(std::vector<NomadString>& names) const {
    names.reserve(names.size() + m_parameterizedConstructors.size());
    for (const auto& registration : m_parameterizedConstructors) {
        names.push_back(registration.name);
    }
}

NomadString TypeManager::getParameterizedConstructorName(
    const NomadString& name,
    const std::vector<const Type*>& arguments
) {
    const auto registration = std::ranges::find(
        m_parameterizedConstructors, name, &ParameterizedConstructorRegistration::name
    );
    const auto hasRegisteredFamily = registration != m_parameterizedConstructors.end();
    const auto hasMatchingArity = hasRegisteredFamily && arguments.size() == registration->argumentCount;
    const auto hasValidArguments = std::ranges::all_of(arguments, [this](const auto* type) {
        return ownsType(type) && !type->isVoid() && !type->isInternal();
    });
    if (!hasMatchingArity || !hasValidArguments) {
        log::error("Cannot resolve parameterized constructor '" + name + "': invalid name or type arguments");
        return {};
    }
    auto specializedName = name + "<";
    for (auto index = NomadIndex{0}; index < arguments.size(); ++index) {
        if (index != 0) {
            specializedName += " ";
        }
        specializedName += arguments[index]->getTypeName();
    }
    specializedName += ">";
    const auto isCached = std::ranges::find(m_constructorSpecializations, specializedName) !=
        m_constructorSpecializations.end();
    if (isCached) {
        return specializedName;
    }
    // Initializers may register more families and invalidate the registration iterator.
    const auto initializer = registration->initializer;
    initializer(specializedName, arguments);
    const auto hasRegisteredConstructor = hasConstructor(specializedName);
    if (!hasRegisteredConstructor) {
        log::error("No constructors registered for '" + specializedName + "'");
        return {};
    }
    m_constructorSpecializations.push_back(specializedName);
    return specializedName;
}

bool TypeManager::registerParameterizedType(
    const NomadString& name,
    const NomadIndex argumentCount,
    ParameterizedTypeFactory factory,
    ParameterizedTypeInitializer initializer
) {
    const auto hasAvailableName = !name.empty() && m_runtime.canRegisterTypeName(name) &&
        getTypeByName(name) == nullptr;
    const auto isConstructorFamily = hasAvailableName &&
        getParameterizedConstructorArgumentCount(name).has_value();
    const auto isTypeFamily = getParameterizedTypeArgumentCount(name).has_value();
    if (!hasAvailableName || argumentCount == 0 || !factory || isConstructorFamily || isTypeFamily) {
        log::error("Cannot register parameterized type '" + name + "': invalid or duplicate definition");
        return false;
    }
    m_parameterizedTypes.push_back({name, argumentCount, std::move(factory), std::move(initializer), std::nullopt, {}});
    m_runtime.publishTypeFamilyNamespace(name, argumentCount);
    return true;
}

std::optional<NomadIndex> TypeManager::getParameterizedTypeArgumentCount(const NomadString& name) const {
    const auto registration = std::ranges::find(m_parameterizedTypes, name, &ParameterizedTypeRegistration::name);
    if (registration == m_parameterizedTypes.end()) {
        return std::nullopt;
    }
    return registration->argumentCount;
}

void TypeManager::getParameterizedTypeNames(std::vector<NomadString>& names) const {
    names.reserve(names.size() + m_parameterizedTypes.size());
    for (const auto& registration : m_parameterizedTypes) {
        names.push_back(registration.name);
    }
}

bool TypeManager::registerConstructorTemplate(
    const NomadString& name,
    std::vector<TemplateParameter> parameters
) {
    const auto registration = std::ranges::find(m_parameterizedTypes, name, &ParameterizedTypeRegistration::name);
    if (registration == m_parameterizedTypes.end()) {
        log::error("Cannot register constructor template '" + name + "': a registered parameterized type is required");
        return false;
    }
    if (registration->constructorTemplate) {
        log::error("Cannot register constructor template '" + name + "': duplicate definition");
        return false;
    }
    std::vector<bool> boundArguments(registration->argumentCount, false);
    for (const auto& parameter : parameters) {
        if (const auto* argument = std::get_if<TypeParameterIndex>(&parameter.type)) {
            if (argument->index >= registration->argumentCount) {
                log::error("Cannot register constructor template '" + name + "': type argument index is out of range");
                return false;
            }
            boundArguments[argument->index] = true;
        } else {
            const auto* type = std::get<const Type*>(parameter.type);
            const auto hasValidType = ownsType(type) && !type->isVoid();
            if (!hasValidType) {
                log::error("Cannot register constructor template '" + name +
                    "': parameter types must be registered and non-void");
                return false;
            }
        }
    }
    if (std::ranges::find(boundArguments, false) != boundArguments.end()) {
        log::error("Cannot register constructor template '" + name + "': every type argument must appear in the signature");
        return false;
    }
    registration->constructorTemplate = std::move(parameters);
    return true;
}

std::optional<std::vector<TemplateParameter>> TypeManager::getConstructorTemplate(const NomadString& name) const {
    const auto registration = std::ranges::find(m_parameterizedTypes, name, &ParameterizedTypeRegistration::name);
    if (registration == m_parameterizedTypes.end()) {
        return std::nullopt;
    }
    return registration->constructorTemplate;
}

ConstructorInferenceResult TypeManager::inferConstructorTypeArguments(
    const NomadString& name,
    const std::vector<const Type*>& argumentTypes
) const {
    const auto registration = std::ranges::find(m_parameterizedTypes, name, &ParameterizedTypeRegistration::name);
    const auto hasRegisteredFamily = registration != m_parameterizedTypes.end();
    const auto hasInferenceSignature = hasRegisteredFamily && registration->constructorTemplate.has_value();
    if (!hasInferenceSignature) {
        return {
            ResolveStatus::UnknownName, {},
            "No constructor inference signature is registered for '" + name + "'"
        };
    }
    const auto& parameters = *registration->constructorTemplate;
    if (argumentTypes.size() != parameters.size()) {
        return {
            ResolveStatus::NoMatchingArity, {},
            "Constructor inference for '" + name + "' expects " + std::to_string(parameters.size()) +
                " argument(s), received " + std::to_string(argumentTypes.size())
        };
    }
    std::vector<const Type*> typeArguments(registration->argumentCount, nullptr);
    for (auto index = NomadIndex{0}; index < parameters.size(); ++index) {
        const auto& parameter = parameters[index];
        const auto* argumentType = argumentTypes[index];
        const auto prefix = "Cannot infer constructor '" + name + "' from argument " +
            std::to_string(index + 1) + " ('" + parameter.name + "'): ";
        if (const auto* typeParameter = std::get_if<TypeParameterIndex>(&parameter.type)) {
            const auto hasValidType = ownsType(argumentType) && !argumentType->isVoid() && !argumentType->isInternal();
            if (!hasValidType) {
                return {
                    ResolveStatus::NoMatchingTypes, {},
                    prefix + "a registered public, non-void argument type is required; supply explicit type arguments"
                };
            }
            auto& binding = typeArguments[typeParameter->index];
            const auto hasConflictingBinding = binding != nullptr && !binding->sameType(argumentType);
            if (hasConflictingBinding) {
                return {
                    ResolveStatus::NoMatchingTypes, {},
                    prefix + "conflicting types for type parameter " + std::to_string(typeParameter->index) +
                        " ('" + binding->getTypeName() + "' and '" + argumentType->getTypeName() + "')"
                };
            }
            if (binding == nullptr) {
                binding = argumentType;
            }
        } else {
            const auto* parameterType = std::get<const Type*>(parameter.type);
            const auto shape = m_runtime.getParameterShape(parameterType);
            const auto isSourceParameter = shape == ParameterShape::SourceFile ||
                shape == ParameterShape::SourceFunction || shape == ParameterShape::SourceLine;
            if (argumentType == nullptr && isSourceParameter) {
                continue;
            }
            const auto hasMatchingType = ownsType(argumentType) && parameterType->sameType(argumentType);
            if (!hasMatchingType) {
                return {
                    ResolveStatus::NoMatchingTypes, {},
                    prefix + "expected exact type '" + parameterType->getTypeName() + "'"
                };
            }
        }
    }
    return {ResolveStatus::Resolved, std::move(typeArguments), {}};
}

const Type* TypeManager::getParameterizedType(
    const NomadString& name,
    const std::vector<const Type*>& arguments
) {
    const auto registration = std::ranges::find(m_parameterizedTypes, name, &ParameterizedTypeRegistration::name);
    if (registration == m_parameterizedTypes.end()) {
        log::error("Unknown parameterized type '" + name + "'");
        return nullptr;
    }
    const auto hasMatchingArity = arguments.size() == registration->argumentCount;
    const auto hasInvalidArguments = hasMatchingArity && std::ranges::any_of(arguments, [this](const Type* type) {
        const auto hasValidType = ownsType(type) && !type->isVoid() && !type->isInternal();
        return !hasValidType;
    });
    if (!hasMatchingArity || hasInvalidArguments) {
        log::error("Invalid type arguments for '" + name + "'");
        return nullptr;
    }
    for (const auto& entry : m_typeSpecializations) {
        const auto hasMatchingSpecialization = entry.name == name && entry.arguments == arguments;
        if (hasMatchingSpecialization) {
            return entry.type;
        }
    }
    // Callbacks can register more families and invalidate the registration iterator.
    const auto factory = registration->factory;
    const auto initializer = registration->initializer;
    const auto id = reserveTypeId();
    auto type = factory(&m_runtime, id, arguments);
    const auto matchesIdentity = type != nullptr && type->getRuntime() == &m_runtime && type->getId() == id;
    const auto hasAvailableName = matchesIdentity && !type->getTypeName().empty() &&
        getTypeByName(type->getTypeName()) == nullptr;
    if (!hasAvailableName) {
        log::error("Factory for '" + name + "' returned an invalid or duplicate type");
        return nullptr;
    }
    const auto* result = type.get();
    registerType(std::move(type));
    if (initializer) {
        initializer(&m_runtime, id);
    }
    m_typeSpecializations.push_back({name, arguments, result});
    return result;
}

bool TypeManager::registerMemberTemplate(
    const NomadString& family,
    const NomadString& name,
    const MemberType type,
    const std::vector<TemplateParameter>& parameters
) {
    const auto registration = std::ranges::find(m_parameterizedTypes, family, &ParameterizedTypeRegistration::name);
    if (registration == m_parameterizedTypes.end()) {
        log::error("Cannot register member template '" + name + "': a registered parameterized type is required");
        return false;
    }
    if (registration->memberTemplates.contains(name)) {
        log::error("Cannot register member template '" + name + "': duplicate definition for '" + family + "'");
        return false;
    }
    std::vector<Parameter> parserParameters;
    for (const auto& parameter : parameters) {
        const Type* parameterType = nullptr;
        if (const auto* index = std::get_if<TypeParameterIndex>(&parameter.type)) {
            if (index->index >= registration->argumentCount) {
                log::error("Cannot register member template '" + name + "': type argument index is out of range");
                return false;
            }
        } else {
            parameterType = std::get<const Type*>(parameter.type);
            const auto hasValidType = ownsType(parameterType) && !parameterType->isVoid();
            if (!hasValidType) {
                log::error("Cannot register member template '" + name + "': parameter types must be registered and non-void");
                return false;
            }
        }
        if (parameter.name.empty()) {
            log::error("Cannot register member template '" + name + "': parameter name is empty");
            return false;
        }
        parserParameters.push_back(defParameter(parameter.name, parameterType, parameter.doc));
    }
    NomadString error;
    if (!m_runtime.canRegisterMemberSignature(name, type, parserParameters, error)) {
        log::error("Cannot register member template '" + name + "': " + error);
        return false;
    }
    registration->memberTemplates.emplace(name, MemberSignature{type, std::move(parserParameters)});
    return true;
}

std::optional<MemberSignature> TypeManager::getMemberTemplateSignature(const NomadString& name) const {
    for (const auto& registration : m_parameterizedTypes) {
        const auto member = registration.memberTemplates.find(name);
        if (member != registration.memberTemplates.end()) {
            return member->second;
        }
    }
    return std::nullopt;
}

const OpaqueIdType* TypeManager::registerOpaqueIdType(const NomadString& name, const NomadString& doc) {
    const auto isReservedByHub = m_runtime.isHub(name) || m_runtime.isHubVariable(name);
    if (isReservedByHub) {
        log::error("Cannot register type '" + name + "': name is reserved by a hub declaration");
        return nullptr;
    }
    if (name.empty()) {
        log::error("Cannot register an opaque ID type with an empty name");
        return nullptr;
    }
    if (getTypeByName(name) != nullptr) {
        log::error("Type with name '" + name + "' already registered");
        return nullptr;
    }
    auto type = std::make_unique<OpaqueIdType>(&m_runtime, reserveTypeId(), name, doc);
    const auto* result = type.get();
    registerType(std::move(type));
    return result;
}

TypeId TypeManager::createEnum(
    NomadString name,
    std::vector<EnumValueDefinition> values,
    NomadString documentation
) {
    if (!m_runtime.canRegisterTypeName(name)) {
        log::error("Cannot create enum '" + name + "': name is reserved by a hub declaration");
        return TypeId::INVALID;
    }
    if (name.empty()) {
        log::error("Cannot create an enum with an empty name");
        return TypeId::INVALID;
    }
    if (getTypeByName(name) != nullptr) {
        log::error("Type with name '" + name + "' already registered");
        return TypeId::INVALID;
    }
    if (values.empty()) {
        log::error("Cannot create enum '" + name + "' without values");
        return TypeId::INVALID;
    }
    for (auto index = NomadIndex{0}; index < values.size(); ++index) {
        const auto& definition = values[index];
        const auto constantName = name + "." + definition.name;
        if (definition.name.empty()) {
            log::error("Cannot create an unnamed value in enum '" + name + "'");
            return TypeId::INVALID;
        }
        if (!m_runtime.canRegisterTypeName(constantName)) {
            log::error("Cannot create enum '" + name + "': value '" + constantName + "' is reserved by a hub");
            return TypeId::INVALID;
        }
        if (m_runtime.getConstantId(constantName).isValid()) {
            log::error("Constant with name '" + constantName + "' already registered");
            return TypeId::INVALID;
        }
        for (auto previousIndex = NomadIndex{0}; previousIndex < index; ++previousIndex) {
            const auto& previousDefinition = values[previousIndex];
            if (previousDefinition.name == definition.name) {
                log::error("Enum '" + name + "' has duplicate value name '" + definition.name + "'");
                return TypeId::INVALID;
            }
            if (previousDefinition.value == definition.value) {
                log::error("Enum '" + name + "' assigns value " + std::to_string(definition.value) + " more than once");
                return TypeId::INVALID;
            }
        }
    }
    const auto id = reserveTypeId();
    auto type = std::make_unique<EnumType>(&m_runtime, id, name, std::move(values), std::move(documentation));
    const auto* result = type.get();
    registerType(std::move(type));
    for (auto index = NomadIndex{0}; index < result->getValueCount(); ++index) {
        const auto& definition = result->getValue(index);
        const auto constantId = m_runtime.registerConstant(
            name + "." + definition.name, RuntimeValue(definition.value), result
        );
        if (constantId.isInvalid()) {
            throw NomadException("Failed to register value '" + definition.name + "' in enum '" + name + "'");
        }
    }
    m_runtime.registerEnumOperators(result);
    return id;
}

const Type* TypeManager::getCallbackType(
    const std::vector<const Type*>& parameterTypes,
    const Type* returnType
) {
    for (const auto& type : m_types) {
        const auto* callback = type != nullptr ? type->asCallback() : nullptr;
        const auto hasMatchingReturnType = callback != nullptr && callback->getReturnType() == returnType;
        const auto hasMatchingArity = hasMatchingReturnType && callback->getParameterCount() == parameterTypes.size();
        if (!hasMatchingArity) {
            continue;
        }
        auto matches = true;
        for (auto index = NomadIndex{0}; index < parameterTypes.size(); ++index) {
            if (callback->getParameterType(index) != parameterTypes[index]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return type.get();
        }
    }
    const auto id = reserveTypeId();
    auto type = std::make_unique<FunctionType>(&m_runtime, id, parameterTypes, returnType);
    const auto* result = type.get();
    registerType(std::move(type));
    return result;
}

const Type* TypeManager::getPredicateType() {
    return getCallbackType({}, m_runtime.getBooleanType());
}

} // namespace nomad
