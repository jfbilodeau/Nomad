// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/VariableContext.hpp>
#include <nomad/script/Variable.hpp>

#include <nomad/log/Logger.hpp>

namespace nomad {

SimpleVariableContext::SimpleVariableContext() = default;

SimpleVariableContext::~SimpleVariableContext() {
    for (NomadIndex index = 0; index < m_values.size(); ++index) {
        if (const auto type = m_variables.getVariableType(VariableId{toNomadId(index)})) {
            type->freeValue(&m_values[index]);
        }
    }
}

NomadIndex SimpleVariableContext::getVariableCount() const {
    return m_variables.getVariableCount();
}

VariableId SimpleVariableContext::getFirstVariableId() const {
    if (m_variables.getVariableCount() == 0) {
        return VariableId::INVALID;
    }

    return VariableId{0};
}

VariableId SimpleVariableContext::getNextVariableId(const VariableId variableId) const {
    if (variableId.id + 1 < toNomadId(getVariableCount())) {
        return VariableId{variableId.id + 1};
    }

    return VariableId::INVALID;
}

VariableId SimpleVariableContext::registerVariable(const NomadString& name, const Type* type) {
    auto variable_id = getVariableId(name);

    if (variable_id.isInvalid()) {
        variable_id = m_variables.registerVariable(name, type);
        m_values.resize(m_variables.getVariableCount());
    } else if (m_variables.getVariableType(variable_id) == nullptr) {
        m_variables.setVariableType(variable_id, type);
    } else if (m_variables.getVariableType(variable_id) != type) {
        log::warning("Context variable '" + name + "' already registered with a different type");
    }

    return variable_id;
}

const NomadString& SimpleVariableContext::getVariableName(VariableId variableId) const {
    return m_variables.getVariableName(variableId);
}

VariableId SimpleVariableContext::getVariableId(const NomadString& name) const {
    return m_variables.getVariableId(name);
}

void SimpleVariableContext::setVariableType(const VariableId variableId, const Type* type) {
    m_variables.setVariableType(variableId, type);
}

const Type* SimpleVariableContext::getVariableType(const VariableId variableId) const {
    return m_variables.getVariableType(variableId);
}

void SimpleVariableContext::setValue(const VariableId variableId, const RuntimeValue& value) {
    const auto* type = m_variables.getVariableType(variableId);

    if (type == nullptr) {
        m_values[toNomadIndex(variableId.id)] = value;
        return;
    }

    type->assignValue(value, m_values[toNomadIndex(variableId.id)]);
}

void SimpleVariableContext::getValue(const VariableId variableId, RuntimeValue& value) {
    value = m_values[toNomadIndex(variableId.id)];
}

bool SimpleVariableContext::isWritten(const VariableId variableId) const {
    return m_variables.isWritten(variableId);
}

void SimpleVariableContext::setWritten(const VariableId variableId, const bool written) {
    m_variables.setWritten(variableId, written);
}

bool SimpleVariableContext::isRead(const VariableId variableId) const {
    return m_variables.isRead(variableId);
}

void SimpleVariableContext::setRead(VariableId variableId, bool read) {
    m_variables.setRead(variableId, read);
}

[[nodiscard]] const VariableMap* SimpleVariableContext::getVariableMap() const {
    return &m_variables;
}

} // nomad
