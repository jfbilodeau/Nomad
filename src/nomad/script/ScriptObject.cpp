// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptObject.hpp>

#include <nomad/script/Property.hpp>
#include <nomad/script/Type.hpp>

namespace nomad {

ScriptObject::ScriptObject(const ClassType* classType):
    m_classType(classType)
{
    if (classType == nullptr) {
        throw NomadException("Script object requires a class type");
    }
    for (const auto* property : classType->getProperties()) {
        if (property->asNativeProperty() == nullptr) {
            m_fieldTypes.push_back(property->getValueType());
        }
    }
    m_values.resize(m_fieldTypes.size());
}

ScriptObject::~ScriptObject() {
    for (auto index = NomadIndex{0}; index < m_values.size(); ++index) {
        if (m_fieldTypes[index]->needsFree()) {
            m_fieldTypes[index]->freeValue(m_values[index]);
        }
    }
}

const ClassType* ScriptObject::getClassType() const {
    return m_classType;
}

NomadIndex ScriptObject::getFieldCount() const {
    return m_values.size();
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
    if (index >= m_values.size()) {
        throw NomadException("Script object field index is out of bounds");
    }
}

} // namespace nomad
