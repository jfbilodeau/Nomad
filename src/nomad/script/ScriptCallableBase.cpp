// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptCallableBase.hpp>

#include <initializer_list>
#include <utility>

namespace nomad {

ScriptCallableBase::ScriptCallableBase(NomadString path, NomadString source):
    m_path(std::move(path)),
    m_source(std::move(source))
{}

ScriptCallableBase::~ScriptCallableBase() = default;

bool ScriptCallableBase::isVariableStoredLocally(const VariableId variableId) const {
    return variableId.isInvalid() ||
        (variableId != m_returnVariables.candidate &&
         variableId != m_returnVariables.assignmentStaging &&
         variableId != m_returnVariables.returnStaging);
}

VariableId ScriptCallableBase::getNamedReturnVariable() const {
    return m_returnVariables.candidate;
}

void ScriptCallableBase::setNamedReturnVariable(
    const VariableId variableId,
    const VariableId assignmentStaging,
    const VariableId returnStaging
) {
    m_returnVariables = {
        .candidate = variableId,
        .assignmentStaging = assignmentStaging,
        .returnStaging = returnStaging
    };
}

NomadIndex ScriptCallableBase::getVariableOffset(const VariableId variableId) const {
    if (!isVariableStoredLocally(variableId)) {
        throw NomadBug("Return-backed or elided variables have no physical local offset");
    }
    auto offset = m_variables.getVariableOffset(variableId);
    for (const auto excluded : {
        m_returnVariables.candidate,
        m_returnVariables.assignmentStaging,
        m_returnVariables.returnStaging
    }) {
        if (excluded.isValid() && excluded.id < variableId.id) {
            offset -= m_variables.getVariableType(excluded)->getSize();
        }
    }
    return offset;
}

NomadIndex ScriptCallableBase::getVariableStorageSize() const {
    auto size = m_variables.getStorageSize();
    for (const auto excluded : {
        m_returnVariables.candidate,
        m_returnVariables.assignmentStaging,
        m_returnVariables.returnStaging
    }) {
        if (excluded.isValid()) {
            size -= m_variables.getVariableType(excluded)->getSize();
        }
    }
    return size;
}

} // namespace nomad
