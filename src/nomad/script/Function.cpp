// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <utility>

#include <nomad/script/Function.hpp>

namespace nomad {
Function::Function(
    NomadId      id,
    NomadString  name,
    NomadString  path,
    NomadString  source
):
    m_id(id),
    m_name(std::move(name)),
    m_path(std::move(path)),
    m_source(std::move(source))
{
}

Function::~Function() = default;


NomadId Function::addParameter(const NomadString& parameterName, const Type* type) {
    m_parameters.registerVariable(parameterName, type);

    return static_cast<NomadId>(m_parameters.getVariableCount() - 1);
}

NomadId Function::getParameterId(const NomadString& parameterName) const {
    return m_parameters.getVariableId(parameterName);
}

const NomadString& Function::getParameterName(const NomadId parameterId) const {
    return m_parameters.getVariableName(parameterId);
}

const Type* Function::getParameterType(const NomadId parameterId) const {
    return m_parameters.getVariableType(parameterId);
}

NomadIndex Function::getParameterCount() const {
    return m_parameters.getVariableCount();
}

NomadId Function::createCapture(
    NomadId id,
    const NomadString& name,
    const CaptureSource source,
    const Type* type
) {
    // Sanity check.

    // Is capture already registered?
    const auto existingCaptureId = getCaptureId(name);
    if (existingCaptureId != NOMAD_INVALID_ID) {
        const auto& capture = m_captures[toNomadIndex(existingCaptureId)];

        if (capture.id != id) {
            throw NomadBug("Internal error: Capture '" + name + "' has a different id than expected");
        }

        if (capture.source != source) {
            throw NomadBug("Internal error: Capture '" + name + "' has a different source than expected");
        }

        if (capture.type != type) {
            throw NomadBug("Internal error: Capture '" + name + "' has a different type than expected");
        }

        return existingCaptureId;
    }

    if (getVariableId(name) != NOMAD_INVALID_ID) {
        throw NomadBug("Internal error: Capture '" + name + "' is already registered as a variable");
    }

    if (getParameterId(name) != NOMAD_INVALID_ID) {
        throw NomadBug("Internal error: Capture '" + name + "' is already registered as a parameter");
    }

    // Sane? Let's register
    m_captures.push_back({name, id, type, source});

    // Let's add the new parameter.
    addParameter(name, type);

    const auto captureId = id = static_cast<NomadId>(m_captures.size() - 1);

    return captureId;
}

NomadId Function::flagParameterAsCaptured(const NomadId parameterId, const NomadString& parameterName, const Type* parameterType) {
    return createCapture(parameterId, parameterName, CaptureSource::Parameter, parameterType);
}

NomadId Function::flagVariableAsCaptured(const NomadId variableId, const NomadString& variableName, const Type* variableType) {
    return createCapture(variableId, variableName, CaptureSource::Variable, variableType);
}

NomadId Function::getCaptureId(const NomadString& name) const {
    for (NomadIndex id = 0; id < m_captures.size(); ++id) {
        if (m_captures[id].name == name) {
            return toNomadId(id);
        }
    }

    return NOMAD_INVALID_ID;
}

NomadId Function::getCaptureParameterId(NomadId captureId) const {
    const auto parameterCount = toNomadId(getParameterCount());
    if (captureId < 0 || captureId >= parameterCount) {
        return NOMAD_INVALID_ID;
    }

    return parameterCount + captureId;
}

const Type* Function::getCaptureType(const NomadId captureId) const {
    const auto index = toNomadIndex(captureId);

    if (index >= m_captures.size()) {
        return nullptr;
    }

    return m_captures[index].type;
}

bool Function::isCaptured(const NomadId id, const CaptureSource source) const {
    for (const auto& capture : m_captures) {
        if (capture.id == id && capture.source == source) {
            return true;
        }
    }
    return false;
}

NomadIndex Function::getCaptureCount() const {
    return m_captures.size();
}

const std::vector<Capture>& Function::getCaptures() const {
    return m_captures;
}

void Function::setParentId(const NomadId parentId) {
    m_parentId = parentId;
}

NomadId Function::getParentId() const {
    return m_parentId;
}

NomadIndex Function::isNested() const {
    return m_parentId != NOMAD_INVALID_ID;
}

NomadId Function::registerVariable(const NomadString& variableName, const Type* type) {
    const auto variableId = getVariableId(variableName);

    if (variableId != NOMAD_INVALID_ID) {
        const auto currentType = getVariableType(variableId);

        if (currentType == nullptr) {
            setVariableType(variableId, type);
        } else if (type != nullptr && currentType != type) {
            throw NomadException("Internal error: Variable '" + variableName + "' already exists with a different type");
        }

        return variableId;
    }

    return m_variables.registerVariable(variableName, type);
}

NomadId Function::getVariableId(const NomadString& variableName) const {
    return m_variables.getVariableId(variableName);
}

[[nodiscard]] const NomadString& Function::getVariableName(NomadId variableId) const {
    return m_variables.getVariableName(variableId);
}

void Function::setVariableType(NomadId variableId, const Type* type) {
    m_variables.setVariableType(variableId, type);
}

const Type* Function::getVariableType(NomadId variableId) const {
    return m_variables.getVariableType(variableId);
}

NomadIndex Function::getVariableCount() const {
    return m_variables.getVariableCount();
}

void Function::setReturnType(const Type* returnType) {
    m_returnType = returnType;
}

const Type* Function::getReturnType() const {
    return m_returnType;
}

void Function::setFunctionStart(NomadIndex functionStartIndex) {
    m_functionStartIndex = functionStartIndex;
}

NomadIndex Function::getFunctionStart() const {
    return m_functionStartIndex;
}

void Function::setFunctionEnd(NomadIndex functionEndIndex) {
    m_functionEndIndex = functionEndIndex;
}

NomadIndex Function::getFunctionEnd() const {
    return m_functionEndIndex;
}

NomadIndex Function::getFunctionLength() const {
    return m_functionEndIndex - m_functionStartIndex;
}

} // nomad
