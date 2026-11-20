// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/TypeManager.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/CompositeType.hpp>
#include <nomad/script/EnumType.hpp>
#include <nomad/script/Runtime.hpp>
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

bool TypeManager::canRegisterClass(
    const NomadString& name,
    const ClassMode mode,
    NomadString& error
) const {
    if (name.empty()) {
        error = "Cannot register a class with an empty name";

        return false;
    }

    if (mode != ClassMode::Opened && mode != ClassMode::Closed) {
        error = "Cannot register class '" + name + "': invalid class mode";

        return false;
    }

    if (getTypeId(name).isValid()) {
        error = "Type with name '" + name + "' already registered";

        return false;
    }

    if (!m_runtime.canRegisterTypeName(name)) {
        error = "Cannot register class '" + name + "': name is reserved by a hub declaration";

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
    NomadString error;
    const auto matchesIdentity = type->getRuntime() == &m_runtime &&
        type->getId() == id && type->getTypeName() == name;
    // Descriptor construction may register other types, so recheck name availability.
    const auto canPublish = matchesIdentity && canRegisterClass(name, mode, error);
    const auto matchesMode = canPublish && type->isOpen() == (mode == ClassMode::Opened);
    if (!canPublish || !matchesMode) {
        if (matchesIdentity && !canPublish) {
            log::error(error);
        } else {
            log::error("Cannot register class '" + name + "': descriptor does not match its registration request");
        }

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
    return registerProperty(owner, std::make_unique<Property>(
        getNextPropertyId(), 
        owner, 
        name, 
        valueType
    ));
}

PropertyId TypeManager::registerProperty(
    CompositeType* owner,
    std::unique_ptr<Property> property
) {
    const auto id = getNextPropertyId();
    if (
        !ownsType(owner) || 
        property == nullptr || 
        id.isInvalid() || 
        property->getId() != id
    ) {
        log::error("Cannot register property: invalid owner, identity, or property capacity");

        return PropertyId::INVALID;
    }

    if (m_properties.size() == m_properties.capacity()) {
        m_properties.reserve(std::max(std::size_t{1}, m_properties.size() * 2));
    }

    auto* registered = property.get();
    if (owner->registerProperty(*this, std::move(property)).isInvalid()) {
        return PropertyId::INVALID;
    }

    m_properties.push_back(registered);

    return id;
}

PropertyId TypeManager::getNextPropertyId() const {
    if (m_properties.size() >= toNomadIndex(NOMAD_ID_MAX)) {
        return PropertyId::INVALID;
    }
    return PropertyId{toNomadId(m_properties.size())};
}

const Property* TypeManager::getProperty(const PropertyId id) const {
    return isIdInRange(id.id, m_properties.size()) ? m_properties[toNomadIndex(id.id)] : nullptr;
}

Property* TypeManager::getMutableProperty(const PropertyId id) {
    return isIdInRange(id.id, m_properties.size()) ? m_properties[toNomadIndex(id.id)] : nullptr;
}

std::vector<const Property*> TypeManager::getProperties() const {
    return {m_properties.begin(), m_properties.end()};
}

const ClassType* TypeManager::getIteratorType(const Type* valueType) {
    const auto hasValidValueType = 
        ownsType(valueType) &&
         !valueType->isVoid() &&
          !valueType->isInternal();

    if (!hasValidValueType) {
        throw NomadException("Iterator value type must be a registered non-void type");
    }

    const auto* type = getParameterizedType("Iterator", {valueType});
    if (type == nullptr) {
        throw NomadException("Failed to specialize iterator type");
    }

    return static_cast<const ClassType*>(type);
}

std::optional<IterableInfo> TypeManager::getIterableInfo(const Type* sourceType) const {
    if (!ownsType(sourceType)) {
        return std::nullopt;
    }
    const auto* owner = sourceType->asClassType();
    return owner == nullptr ? std::nullopt : owner->getIterableInfo();
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
