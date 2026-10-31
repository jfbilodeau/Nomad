// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/CompositeType.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/Runtime.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace nomad {

CompositeType::CompositeType(
    Runtime* runtime,
    TypeId id,
    NomadString name,
    const ClassMode mode,
    NomadString documentation
):
    Type(runtime, id, std::move(documentation)),
    m_name(std::move(name)),
    m_initializerName(m_name + "$init"),
    m_mode(mode)
{}

CompositeType::~CompositeType() = default;

NomadString CompositeType::getTypeName() const {
    return m_name;
}

const NomadString& CompositeType::getInitializerName() const {
    return m_initializerName;
}

const CompositeType* CompositeType::asCompositeType() const {
    return this;
}

bool CompositeType::isOpen() const {
    return m_mode == ClassMode::Opened;
}

CallableId CompositeType::registerConstructor(
    Runtime& runtime,
    std::unique_ptr<Constructor> constructor
) {
    if (constructor == nullptr || constructor->getClassType() != this ||
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

std::vector<const Constructor*> CompositeType::getConstructors() const {
    std::vector<const Constructor*> constructors;
    constructors.reserve(m_constructors.size());
    for (const auto& constructor : m_constructors) {
        constructors.push_back(constructor.get());
    }
    return constructors;
}

CallableId CompositeType::registerMethod(
    Runtime& runtime,
    std::unique_ptr<Method> method
) {
    if (method == nullptr || method->getClassType() != this ||
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

std::vector<const Method*> CompositeType::getMethods() const {
    std::vector<const Method*> methods;
    methods.reserve(m_methods.size());
    for (const auto& method : m_methods) {
        methods.push_back(method.get());
    }
    return methods;
}

PropertyId CompositeType::registerProperty(
    Runtime& runtime,
    std::unique_ptr<Property> property
) {
    const auto* valueType = property == nullptr ? nullptr : property->getValueType();
    const auto* native = property == nullptr ? nullptr : property->asNativeProperty();
    const auto deferred = native != nullptr && !native->isTypeResolved() &&
        valueType != nullptr && valueType->getRuntime() == &runtime;
    if (property == nullptr || property->getOwnerType() != this || property->getId().isValid() ||
        runtime.getType(getId()) != this ||
        valueType == nullptr || (!deferred && (valueType->isVoid() || valueType->isInternal() ||
            runtime.getType(valueType->getId()) != valueType)) ||
        getProperty(property->getName()) != nullptr ||
        std::ranges::any_of(m_methods, [&](const auto& method) { return method->getName() == property->getName(); })) {
        log::error("Cannot register property: invalid owner, value type, or duplicate definition");
        return PropertyId::INVALID;
    }
    if (native == nullptr && m_fieldLayoutState != FieldLayoutState::Unresolved) {
        log::error("Cannot register stored property '" + property->getName() + "': field layout of '" +
            getTypeName() + "' is being finalized or already finalized");
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

const Property* CompositeType::getProperty(const NomadString& name) const {
    const auto property = std::find_if(
        m_properties.begin(),
        m_properties.end(),
        [&](const auto& candidate) {
            return candidate->getName() == name;
        }
    );
    return property == m_properties.end() ? nullptr : property->get();
}

const Property* CompositeType::getProperty(const PropertyId id) const {
    const auto property = std::find_if(m_properties.begin(), m_properties.end(), [id](const auto& candidate) {
        return candidate->getId() == id;
    });
    return property == m_properties.end() ? nullptr : property->get();
}

std::vector<const Property*> CompositeType::getProperties() const {
    std::vector<const Property*> properties;
    properties.reserve(m_properties.size());
    for (const auto& property : m_properties) {
        properties.push_back(property.get());
    }
    return properties;
}

NomadIndex CompositeType::getPropertyCount() const {
    return static_cast<NomadIndex>(m_properties.size());
}

NomadIndex CompositeType::getFieldIndex(const PropertyId id) const {
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

NomadIndex CompositeType::getFieldOffset(const PropertyId id) const {
    const auto* field = getProperty(id);
    if (field == nullptr || field->asNativeProperty() != nullptr) {
        throw NomadException("Cannot calculate field offset: property ID does not identify a stored field of '" +
            getTypeName() + "'");
    }

    auto offset = NomadIndex{0};
    for (const auto& property : m_properties) {
        if (property.get() == field) {
            break;
        }
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        const auto size = property->getValueType()->getSize();
        if (size > std::numeric_limits<NomadIndex>::max() - offset) {
            throw NomadException("Field storage size exceeds the maximum slot count at '" + property->getName() + "'");
        }
        offset += size;
    }
    return offset;
}

NomadIndex CompositeType::getFieldStorageSize() const {
    if (isFieldLayoutFinalized()) {
        return m_finalizedFieldStorageSize;
    }

    auto size = NomadIndex{0};
    for (const auto& property : m_properties) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }
        const auto fieldSize = property->getValueType()->getSize();
        if (fieldSize > std::numeric_limits<NomadIndex>::max() - size) {
            throw NomadException("Field storage size exceeds the maximum slot count at '" + property->getName() + "'");
        }
        size += fieldSize;
    }
    return size;
}

bool CompositeType::isFieldLayoutFinalized() const {
    return m_fieldLayoutState == FieldLayoutState::Finalized;
}

void CompositeType::finalizeFieldLayout() const {
    if (isFieldLayoutFinalized()) {
        return;
    }
    if (m_fieldLayoutState == FieldLayoutState::Resolving) {
        throw NomadException("Inline struct field layout cycle involving '" + getTypeName() + "'");
    }

    m_fieldLayoutState = FieldLayoutState::Resolving;
    try {
        auto size = NomadIndex{0};
        for (const auto& property : m_properties) {
            if (property->asNativeProperty() != nullptr) {
                continue;
            }
            const auto* type = property->getValueType();
            if (type->getStorage() == TypeStorage::Struct) {
                const auto* composite = type->asCompositeType();
                if (composite == nullptr) {
                    throw NomadException("Cannot finalize field '" + getTypeName() + "." + property->getName() +
                        "': inline struct type has no composite metadata");
                }
                composite->finalizeFieldLayout();
            }
            const auto fieldSize = type->getSize();
            if (fieldSize > std::numeric_limits<NomadIndex>::max() - size) {
                throw NomadException("Field storage size exceeds the maximum slot count at '" + property->getName() + "'");
            }
            size += fieldSize;
        }
        m_finalizedFieldStorageSize = size;
        m_fieldLayoutState = FieldLayoutState::Finalized;
    } catch (...) {
        m_fieldLayoutState = FieldLayoutState::Unresolved;
        throw;
    }
}

} // namespace nomad
