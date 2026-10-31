// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/TypeManager.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/CompositeType.hpp>
#include <nomad/script/Runtime.hpp>

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

} // namespace nomad
