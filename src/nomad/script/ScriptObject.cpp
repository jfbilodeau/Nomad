// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptObject.hpp>

#include <nomad/script/Property.hpp>
#include <nomad/script/Type.hpp>

namespace nomad {

ScriptObject::ScriptObject(const ObjectType* objectType):
    m_objectType(objectType)
{
    if (objectType == nullptr) {
        throw NomadException("Script object requires an object type");
    }
    for (const auto* property : objectType->getProperties()) {
        if (property->asNativeProperty() == nullptr) {
            m_fieldTypes.push_back(property->getValueType());
        }
    }
    m_values.resize(m_fieldTypes.size());
}

ScriptObject::~ScriptObject() {
    for (auto index = NomadIndex{0}; index < m_initializedFieldCount; ++index) {
        if (m_fieldTypes[index]->needsFree()) {
            m_fieldTypes[index]->freeValue(m_values[index]);
        }
    }
}

const ObjectType* ScriptObject::getObjectType() const {
    return m_objectType;
}

NomadIndex ScriptObject::getFieldCount() const {
    return m_values.size();
}

void ScriptObject::initializeFields() {
    if (m_initializationState != InitializationState::Allocated) {
        throw NomadException("Script object fields can only be initialized once");
    }
    m_initializationState = InitializationState::Initializing;
    for (auto index = NomadIndex{0}; index < m_values.size(); ++index) {
        // Include the current slot in cleanup if its initializer stores a value before throwing.
        ++m_initializedFieldCount;
        m_fieldTypes[index]->initValue(m_values[index]);
    }
    m_initializationState = InitializationState::Initialized;
}

const RuntimeValue& ScriptObject::getField(const NomadIndex index) const {
    validateFieldAccess(index);
    return m_values[index];
}

void ScriptObject::setField(const NomadIndex index, const RuntimeValue& value) {
    validateFieldAccess(index);
    m_fieldTypes[index]->assignValue(value, m_values[index]);
}

void ScriptObject::validateFieldAccess(const NomadIndex index) const {
    if (m_initializationState != InitializationState::Initialized) {
        throw NomadException("Script object fields are not initialized");
    }
    if (index >= m_values.size()) {
        throw NomadException("Script object field index is out of bounds");
    }
}

} // namespace nomad
