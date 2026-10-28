// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Property.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Runtime.hpp>

#include <utility>

namespace nomad {

Property::Property(
    const ClassType* classType,
    NomadString name,
    const Type* valueType,
    NomadString doc
):
    m_classType(classType),
    m_name(std::move(name)),
    m_valueType(valueType),
    m_doc(std::move(doc))
{}

Property::~Property() = default;

PropertyId Property::getId() const {
    return m_id;
}

const ClassType* Property::getClassType() const {
    return m_classType;
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

bool Property::hasExplicitInitializer() const {
    return m_hasExplicitInitializer;
}

void Property::setHasExplicitInitializer(const bool hasExplicitInitializer) {
    m_hasExplicitInitializer = hasExplicitInitializer;
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
