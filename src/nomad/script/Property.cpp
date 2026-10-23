// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Property.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Runtime.hpp>

#include <utility>

namespace nomad {

Property::Property(
    const ObjectType* objectType,
    NomadString name,
    const Type* valueType,
    NomadString doc
):
    m_objectType(objectType),
    m_name(std::move(name)),
    m_valueType(valueType),
    m_doc(std::move(doc))
{}

Property::~Property() = default;

PropertyId Property::getId() const {
    return m_id;
}

const ObjectType* Property::getObjectType() const {
    return m_objectType;
}

const NomadString& Property::getName() const {
    return m_name;
}

const Type* Property::getValueType() const {
    return m_valueType;
}

const NomadString& Property::getDoc() const {
    return m_doc;
}

const NativeProperty* Property::asNativeProperty() const {
    return nullptr;
}

PropertyId Property::registerWithRuntime(Runtime& runtime) {
    if (m_id.isValid()) {
        log::error("Cannot register property: property is already registered");
        return PropertyId::INVALID;
    }
    m_id = runtime.catalogProperty(this);
    return m_id;
}

} // namespace nomad
