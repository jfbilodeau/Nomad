// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Variable.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/system/String.hpp>

#include <limits>

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

VariableId VariableMap::registerVariable(const NomadString& name, const Type* type) {
    auto variableId = getVariableId(name);

    if (variableId.isInvalid()) {
        variableId = VariableId{toNomadId(m_variables.size())};
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

VariableId VariableMap::getVariableId(const NomadString& name) const {
    for (NomadIndex i = 0; i < m_variables.size(); ++i) {
        if (m_variables[i].getName() == name) {
            return VariableId{toNomadId(i)};
        }
    }

    return VariableId::INVALID;
}

const NomadString& VariableMap::getVariableName(const VariableId variableId) const {
    return m_variables[toNomadIndex(variableId.id)].getName();
}

void VariableMap::setVariableType(const VariableId variableId, const Type* type) {
    m_variables[toNomadIndex(variableId.id)].setType(type);
}

const Type* VariableMap::getVariableType(const VariableId variableId) const {
    return m_variables[toNomadIndex(variableId.id)].getType();
}

NomadIndex VariableMap::getVariableCount() const {
    return m_variables.size();
}

NomadIndex VariableMap::getVariableOffset(const VariableId variableId) const {
    if (isIdOutOfRange(variableId.id, m_variables.size())) {
        throw NomadException("Variable ID out of range");
    }

    auto offset = NomadIndex{0};
    for (auto index = NomadIndex{0}; index < toNomadIndex(variableId.id); ++index) {
        const auto& variable = m_variables[index];
        const auto* type = variable.getType();
        if (type == nullptr) {
            throw NomadException("Cannot calculate variable layout: type of '" + variable.getName() + "' is unresolved");
        }
        const auto size = type->getSize();
        if (size > std::numeric_limits<NomadIndex>::max() - offset) {
            throw NomadException("Variable storage size exceeds the maximum slot count at '" + variable.getName() + "'");
        }
        offset += size;
    }
    return offset;
}

NomadIndex VariableMap::getStorageSize() const {
    auto size = NomadIndex{0};
    for (const auto& variable : m_variables) {
        const auto* type = variable.getType();
        if (type == nullptr) {
            throw NomadException("Cannot calculate variable layout: type of '" + variable.getName() + "' is unresolved");
        }
        const auto variableSize = type->getSize();
        if (variableSize > std::numeric_limits<NomadIndex>::max() - size) {
            throw NomadException("Variable storage size exceeds the maximum slot count at '" + variable.getName() + "'");
        }
        size += variableSize;
    }
    return size;
}

bool VariableMap::isWritten(const VariableId variableId) const {
    return m_variables[toNomadIndex(variableId.id)].isWritten();
}

void VariableMap::setWritten(const VariableId variableId, const bool written) {
    m_variables[toNomadIndex(variableId.id)].setWritten(written);
}

bool VariableMap::isRead(const VariableId variableId) const {
    return m_variables[toNomadIndex(variableId.id)].isRead();
}

void VariableMap::setRead(const VariableId variableId, const bool read) {
    m_variables[toNomadIndex(variableId.id)].setRead(read);
}

VariableList::VariableList(const VariableMap* variableMap):
    m_variableMap(*variableMap) {
    m_variableValues.resize(m_variableMap.getVariableCount());

    for (NomadIndex i = 0; i < m_variableMap.getVariableCount(); ++i) {
        const auto variableId = VariableId{toNomadId(i)};
        const auto type = m_variableMap.getVariableType(variableId);

        if (type != nullptr) {
            type->initValue(&m_variableValues[i]);
        } else {
            log::warning("Initializing variable ID " + toString(variableId.id) + " with null type" );
        }
    }
}

VariableList::~VariableList() {
    for (NomadIndex i = 0; i < m_variableMap.getVariableCount(); ++i) {
        const auto variableId = VariableId{toNomadId(i)};

        auto type = m_variableMap.getVariableType(variableId);

        type->freeValue(&m_variableValues[i]);
    }
}

VariableId VariableList::getVariableId(const NomadString& name) const {
    return m_variableMap.getVariableId(name);
}

const NomadString& VariableList::getVariableName(VariableId variableId) const {
    return m_variableMap.getVariableName(variableId);
}

const Type* VariableList::getVariableType(VariableId variableId) const {
    return m_variableMap.getVariableType(variableId);
}

NomadIndex VariableList::getVariableCount() const {
    return m_variableMap.getVariableCount();
}

void VariableList::setVariableValue(const VariableId variableId, const RuntimeValue& value) {
    const auto variableIndex = toNomadIndex(variableId.id);

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

    type->assignValue(&value, &m_variableValues[variableIndex]);
}

void VariableList::getVariableValue(const VariableId variableId, RuntimeValue& value) const {
    const auto variableIndex = toNomadIndex(variableId.id);

#ifdef NOMAD_DEBUG
    if (variableIndex >= m_variableValues.size()) {
        log::error("Variable ID out of range");

        return;
    }
#endif

    value = m_variableValues[variableIndex];
}

} // nomad
