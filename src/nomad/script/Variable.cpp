// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/Logger.hpp>

#include <nomad/script/Variable.hpp>

#include <nomad/system/String.hpp>

namespace nomad {

VariableDefinition::VariableDefinition(const NomadString& name, const Type* type):
    m_name(name),
    m_type(type)
{}

const NomadString& VariableDefinition::getName() const {
    return m_name;
}

void VariableDefinition::setType(const Type* type) {
    m_type = type;
}

const Type* VariableDefinition::getType() const {
    return m_type;
}

bool VariableDefinition::isWritten() const {
    return m_isWritten;
}

void VariableDefinition::setWritten(const bool written) {
    m_isWritten = written;
}

bool VariableDefinition::isRead() const {
    return m_isRead;
}

void VariableDefinition::setRead(const bool read) {
    m_isRead = read;
}

NomadId VariableMap::registerVariable(const NomadString& name, const Type* type) {
    auto variableId = getVariableId(name);

    if (variableId == NOMAD_INVALID_ID) {
        variableId = toNomadId(m_variables.size());
        m_variables.emplace_back(name, type);
    } else {
        const auto existingType = getVariableType(variableId);

        if (existingType == nullptr) {
            setVariableType(variableId, type);
        } else if (type != nullptr && existingType != type) {
            const auto& originalType = existingType->getTypeName();
            const auto& newType = type->getTypeName();

            log::error("Redefining variable `" + name + "` from `" + originalType + "` to `" + newType + "`. Ignoring");
        }
    }

    return variableId;
}

NomadId VariableMap::getVariableId(const NomadString& name) const {
    for (NomadIndex i = 0; i < m_variables.size(); ++i) {
        if (m_variables[i].getName() == name) {
            return toNomadId(i);
        }
    }

    return NOMAD_INVALID_ID;
}

const NomadString& VariableMap::getVariableName(const NomadId variableId) const {
    return m_variables[toNomadIndex(variableId)].getName();
}

void VariableMap::setVariableType(const NomadId variableId, const Type* type) {
    m_variables[toNomadIndex(variableId)].setType(type);
}

const Type* VariableMap::getVariableType(const NomadId variableId) const {
    return m_variables[toNomadIndex(variableId)].getType();
}

NomadIndex VariableMap::getVariableCount() const {
    return m_variables.size();
}

bool VariableMap::isWritten(const NomadId variableId) const {
    return m_variables[toNomadIndex(variableId)].isWritten();
}

void VariableMap::setWritten(const NomadId variableId, const bool written) {
    m_variables[toNomadIndex(variableId)].setWritten(written);
}

bool VariableMap::isRead(const NomadId variableId) const {
    return m_variables[toNomadIndex(variableId)].isRead();
}

void VariableMap::setRead(const NomadId variableId, const bool read) {
    m_variables[toNomadIndex(variableId)].setRead(read);
}

VariableList::VariableList(const VariableMap* variableMap):
    m_variableMap(*variableMap) {
    m_variableValues.resize(m_variableMap.getVariableCount());

    for (NomadIndex i = 0; i < m_variableMap.getVariableCount(); ++i) {
        const NomadId variableId = toNomadId(i);
        const auto type = m_variableMap.getVariableType(variableId);

        if (type != nullptr) {
            type->initValue(m_variableValues[i]);
        } else {
            log::warning("Initializing variable ID " + toString(variableId) + " with null type" );
        }
    }
}

VariableList::~VariableList() {
    for (NomadIndex i = 0; i < m_variableMap.getVariableCount(); ++i) {
        const NomadId variableId = toNomadId(i);

        auto type = m_variableMap.getVariableType(variableId);

        type->freeValue(m_variableValues[i]);
    }
}

NomadId VariableList::getVariableId(const NomadString& name) const {
    return m_variableMap.getVariableId(name);
}

const NomadString& VariableList::getVariableName(NomadId variableId) const {
    return m_variableMap.getVariableName(variableId);
}

const Type* VariableList::getVariableType(NomadId variableId) const {
    return m_variableMap.getVariableType(variableId);
}

NomadIndex VariableList::getVariableCount() const {
    return m_variableMap.getVariableCount();
}

void VariableList::setVariableValue(const NomadId variableId, const RuntimeValue& value) {
    const auto variableIndex = toNomadIndex(variableId);

#ifdef NOMAD_DEBUG
    if (variableIndex >= m_variableValues.size()) {
        log::error("Variable ID out of range");
    }
#endif

    const auto* type = m_variableMap.getVariableType(variableId);

    if (type == nullptr) {
        m_variableValues[variableIndex] = value;
        return;
    }

    type->assignValue(value, m_variableValues[variableIndex]);
}

void VariableList::getVariableValue(const NomadId variableId, RuntimeValue& value) const {
    const auto variableIndex = toNomadIndex(variableId);

#ifdef NOMAD_DEBUG
    if (variableIndex >= m_variableValues.size()) {
        log::error("Variable ID out of range");

        return;
    }
#endif

    value = m_variableValues[variableIndex];
}

} // nomad
