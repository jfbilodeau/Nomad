// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptFunction.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Callable.hpp>

#include <utility>

namespace nomad {

ScriptFunction::ScriptFunction(
    NomadId      id,
    NomadString  name,
    NomadString  path,
    NomadString  source
):
    m_ownedDefinition(std::make_unique<ScriptFunctionDefinition>()),
    m_definition(m_ownedDefinition.get()),
    m_path(std::move(path)),
    m_source(std::move(source))
{
    m_definition->id = id;
    m_definition->name = std::move(name);
    m_definition->function = this;
}

ScriptFunction::~ScriptFunction() = default;

void ScriptFunction::attachDefinition(ScriptFunctionDefinition* definition) {
    definition->function = this;
    m_definition = definition;
    m_ownedDefinition.reset();
}

NomadId ScriptFunction::getId() const {
    return m_definition->id;
}

const NomadString& ScriptFunction::getName() const {
    return m_definition->name;
}

const NomadString& ScriptFunction::getPath() const {
    return m_path;
}

const NomadString& ScriptFunction::getSource() const {
    return m_source;
}

void ScriptFunction::setSourceLineRange(const NomadIndex startLine, const NomadIndex endLine) {
    m_sourceStartLine = startLine;
    m_sourceEndLine = endLine;
}

NomadIndex ScriptFunction::getSourceStartLine() const {
    return m_sourceStartLine;
}

NomadIndex ScriptFunction::getSourceEndLine() const {
    return m_sourceEndLine;
}

NomadId ScriptFunction::addParameter(const NomadString& parameterName, const Type* type) {
    auto parameterId = getParameterId(parameterName);

    if (parameterId == NOMAD_INVALID_ID) {
        parameterId = toNomadId(m_definition->parameters.size());
        m_definition->parameters.push_back({parameterName, type});
    } else {
        auto& existing = m_definition->parameters[toNomadIndex(parameterId)];

        if (existing.type == nullptr) {
            existing.type = type;
        } else if (type != nullptr && existing.type != type) {
            log::error(
                "Redefining variable `" + parameterName + "` from `" + existing.type->getTypeName() +
                "` to `" + type->getTypeName() + "`. Ignoring"
            );
        }
    }

    return parameterId;
}

NomadId ScriptFunction::getParameterId(const NomadString& parameterName) const {
    const auto& parameters = m_definition->parameters;

    for (NomadIndex index = 0; index < parameters.size(); ++index) {
        if (parameters[index].name == parameterName) {
            return toNomadId(index);
        }
    }

    return NOMAD_INVALID_ID;
}

const NomadString& ScriptFunction::getParameterName(const NomadId parameterId) const {
    return m_definition->parameters[toNomadIndex(parameterId)].name;
}

const Type* ScriptFunction::getParameterType(const NomadId parameterId) const {
    return m_definition->parameters[toNomadIndex(parameterId)].type;
}

NomadIndex ScriptFunction::getParameterCount() const {
    return m_definition->parameters.size();
}

NomadId ScriptFunction::createCapture(
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

NomadId ScriptFunction::flagParameterAsCaptured(const NomadId parameterId, const NomadString& parameterName, const Type* parameterType) {
    return createCapture(parameterId, parameterName, CaptureSource::Parameter, parameterType);
}

NomadId ScriptFunction::flagVariableAsCaptured(const NomadId variableId, const NomadString& variableName, const Type* variableType) {
    return createCapture(variableId, variableName, CaptureSource::Variable, variableType);
}

NomadId ScriptFunction::getCaptureId(const NomadString& name) const {
    for (NomadIndex id = 0; id < m_captures.size(); ++id) {
        if (m_captures[id].name == name) {
            return toNomadId(id);
        }
    }

    return NOMAD_INVALID_ID;
}

NomadId ScriptFunction::getCaptureParameterId(NomadId captureId) const {
    const auto parameterCount = toNomadId(getParameterCount());
    if (captureId < 0 || captureId >= parameterCount) {
        return NOMAD_INVALID_ID;
    }

    return parameterCount + captureId;
}

const Type* ScriptFunction::getCaptureType(const NomadId captureId) const {
    const auto index = toNomadIndex(captureId);

    if (index >= m_captures.size()) {
        return nullptr;
    }

    return m_captures[index].type;
}

bool ScriptFunction::isCaptured(const NomadId id, const CaptureSource source) const {
    for (const auto& capture : m_captures) {
        if (capture.id == id && capture.source == source) {
            return true;
        }
    }
    return false;
}

NomadIndex ScriptFunction::getCaptureCount() const {
    return m_captures.size();
}

const std::vector<Capture>& ScriptFunction::getCaptures() const {
    return m_captures;
}

void ScriptFunction::setParentId(const NomadId parentId) {
    m_parentId = parentId;
}

NomadId ScriptFunction::getParentId() const {
    return m_parentId;
}

NomadIndex ScriptFunction::isNested() const {
    return m_parentId != NOMAD_INVALID_ID;
}

NomadId ScriptFunction::registerVariable(const NomadString& variableName, const Type* type) {
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

NomadId ScriptFunction::getVariableId(const NomadString& variableName) const {
    return m_variables.getVariableId(variableName);
}

[[nodiscard]] const NomadString& ScriptFunction::getVariableName(NomadId variableId) const {
    return m_variables.getVariableName(variableId);
}

void ScriptFunction::setVariableType(NomadId variableId, const Type* type) {
    m_variables.setVariableType(variableId, type);
}

const Type* ScriptFunction::getVariableType(NomadId variableId) const {
    return m_variables.getVariableType(variableId);
}

NomadIndex ScriptFunction::getVariableCount() const {
    return m_variables.getVariableCount();
}

void ScriptFunction::setReturnType(const Type* returnType) {
    m_definition->returnType = returnType;
}

const Type* ScriptFunction::getReturnType() const {
    return m_definition->returnType;
}

void ScriptFunction::setFunctionStart(NomadIndex functionStartIndex) {
    m_functionStartIndex = functionStartIndex;
}

NomadIndex ScriptFunction::getFunctionStart() const {
    return m_functionStartIndex;
}

void ScriptFunction::setFunctionEnd(NomadIndex functionEndIndex) {
    m_functionEndIndex = functionEndIndex;
}

NomadIndex ScriptFunction::getFunctionEnd() const {
    return m_functionEndIndex;
}

NomadIndex ScriptFunction::getFunctionLength() const {
    return m_functionEndIndex - m_functionStartIndex;
}

} // nomad
