// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/log/Logger.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/Variable.hpp>

#include <utility>
#include <vector>

namespace nomad {

enum class CaptureSource {
    Variable = 1,
    Parameter,
};

struct Capture {
    NomadString name;
    union {
        VariableId variableId = VariableId::INVALID;
        NomadId parameterId;
    };
    const Type* type = nullptr;
    CaptureSource source = CaptureSource::Variable;
};

class ScriptCallableBase {
public:
    ScriptCallableBase(const ScriptCallableBase&) = delete;
    ScriptCallableBase& operator=(const ScriptCallableBase&) = delete;
    virtual ~ScriptCallableBase() = default;

    [[nodiscard]] virtual CallableId getId() const = 0;
    [[nodiscard]] virtual const NomadString& getName() const = 0;
    [[nodiscard]] virtual const std::vector<Parameter>& getParameters() const = 0;
    [[nodiscard]] virtual NomadIndex getParameterCount() const = 0;
    [[nodiscard]] virtual const Type* getReturnType() const = 0;
    [[nodiscard]] virtual const Type* getObjectType() const = 0;
    virtual void setReturnType(const Type* returnType) = 0;

    [[nodiscard]] const NomadString& getPath() const { return m_path; }
    [[nodiscard]] const NomadString& getSourceCode() const { return m_source; }
    [[nodiscard]] const NomadString& getHub() const { return m_hub; }
    void setHub(NomadString hub) { m_hub = std::move(hub); }

    void setSourceLineRange(const NomadIndex startLine, const NomadIndex endLine) {
        m_sourceStartLine = startLine;
        m_sourceEndLine = endLine;
    }

    [[nodiscard]] NomadIndex getSourceStartLine() const { return m_sourceStartLine; }
    [[nodiscard]] NomadIndex getSourceEndLine() const { return m_sourceEndLine; }

    NomadId addParameter(
        const NomadString& parameterName,
        const Type* type,
        const NomadString& doc
    ) {
        auto parameterId = getParameterId(parameterName);
        auto& parameters = this->getEditableParameters();

        if (parameterId == NOMAD_INVALID_ID) {
            parameterId = toNomadId(parameters.size());
            parameters.push_back(defParameter(parameterName, type, doc));
        } else {
            auto& existing = parameters[toNomadIndex(parameterId)];
            if (existing.type == nullptr) {
                existing.type = type;
            } else if (type != nullptr && existing.type != type) {
                log::error(
                    "Redefining variable `" + parameterName + "` from `" + existing.type->getTypeName() +
                    "` to `" + type->getTypeName() + "`. Ignoring"
                );
            }
        }

        registerVariable(parameterName, type);
        return parameterId;
    }

    [[nodiscard]] NomadId getParameterId(const NomadString& parameterName) const {
        const auto& parameters = this->getParameters();
        for (NomadIndex index = 0; index < parameters.size(); ++index) {
            if (parameters[index].name == parameterName) {
                return toNomadId(index);
            }
        }
        return NOMAD_INVALID_ID;
    }

    [[nodiscard]] const NomadString& getParameterName(const NomadId parameterId) const {
        return this->getParameters()[toNomadIndex(parameterId)].name;
    }

    [[nodiscard]] const Type* getParameterType(const NomadId parameterId) const {
        return this->getParameters()[toNomadIndex(parameterId)].type;
    }

    NomadId createCapture(
        const VariableId variableId,
        const NomadString& name,
        const Type* type
    ) {
        Capture capture;
        capture.name = name;
        capture.variableId = variableId;
        capture.type = type;
        return registerCapture(capture);
    }

    NomadId createCapture(
        const NomadId parameterId,
        const NomadString& name,
        const Type* type
    ) {
        Capture capture;
        capture.name = name;
        capture.parameterId = parameterId;
        capture.type = type;
        capture.source = CaptureSource::Parameter;
        return registerCapture(capture);
    }

    NomadId flagParameterAsCaptured(
        const NomadId parameterId,
        const NomadString& parameterName,
        const Type* parameterType
    ) {
        return createCapture(parameterId, parameterName, parameterType);
    }

    NomadId flagVariableAsCaptured(
        const VariableId variableId,
        const NomadString& variableName,
        const Type* variableType
    ) {
        return createCapture(variableId, variableName, variableType);
    }

    [[nodiscard]] NomadId getCaptureId(const NomadString& name) const {
        for (NomadIndex index = 0; index < m_captures.size(); ++index) {
            if (m_captures[index].name == name) {
                return toNomadId(index);
            }
        }
        return NOMAD_INVALID_ID;
    }

    [[nodiscard]] NomadId getCaptureParameterId(const NomadId captureId) const {
        const auto parameterCount = toNomadId(this->getParameterCount());
        if (captureId < 0 || captureId >= parameterCount) {
            return NOMAD_INVALID_ID;
        }
        return parameterCount + captureId;
    }

    [[nodiscard]] const Type* getCaptureType(const NomadId captureId) const {
        const auto index = toNomadIndex(captureId);
        return index < m_captures.size() ? m_captures[index].type : nullptr;
    }

    [[nodiscard]] bool isCaptured(const VariableId variableId) const {
        for (const auto& capture : m_captures) {
            if (capture.source == CaptureSource::Variable && capture.variableId == variableId) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool isCaptured(const NomadId parameterId) const {
        for (const auto& capture : m_captures) {
            if (capture.source == CaptureSource::Parameter && capture.parameterId == parameterId) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] NomadIndex getCaptureCount() const { return m_captures.size(); }
    [[nodiscard]] const std::vector<Capture>& getCaptures() const { return m_captures; }

    void setParentId(const CallableId parentId) { m_parentId = parentId; }
    [[nodiscard]] CallableId getParentId() const { return m_parentId; }
    [[nodiscard]] NomadIndex isNested() const { return m_parentId != CallableId{}; }

    VariableId registerVariable(const NomadString& variableName, const Type* type) {
        const auto variableId = getVariableId(variableName);
        if (variableId.isValid()) {
            const auto* currentType = getVariableType(variableId);
            if (currentType == nullptr) {
                setVariableType(variableId, type);
            } else if (type != nullptr && currentType != type) {
                throw NomadException("Internal error: Variable '" + variableName + "' already exists with a different type");
            }
            return variableId;
        }
        return m_variables.registerVariable(variableName, type);
    }

    [[nodiscard]] VariableId getVariableId(const NomadString& variableName) const {
        return m_variables.getVariableId(variableName);
    }

    [[nodiscard]] const NomadString& getVariableName(const VariableId variableId) const {
        return m_variables.getVariableName(variableId);
    }

    void setVariableType(const VariableId variableId, const Type* type) {
        m_variables.setVariableType(variableId, type);
    }

    [[nodiscard]] const Type* getVariableType(const VariableId variableId) const {
        return m_variables.getVariableType(variableId);
    }

    [[nodiscard]] NomadIndex getVariableCount() const { return m_variables.getVariableCount(); }

    [[nodiscard]] NomadString getVisibleVariableName(VariableId variableId) const {
        const auto& storageName = getVariableName(variableId);
        return storageName.find('@') == NomadString::npos ? storageName : NomadString{};
    }

    void setFunctionStart(const NomadIndex functionStartIndex) { m_functionStartIndex = functionStartIndex; }
    [[nodiscard]] NomadIndex getFunctionStart() const { return m_functionStartIndex; }
    void setFunctionEnd(const NomadIndex functionEndIndex) { m_functionEndIndex = functionEndIndex; }
    [[nodiscard]] NomadIndex getFunctionEnd() const { return m_functionEndIndex; }
    [[nodiscard]] NomadIndex getFunctionLength() const { return m_functionEndIndex - m_functionStartIndex; }

protected:
    ScriptCallableBase(
        NomadString path,
        NomadString source
    ):
        m_path(std::move(path)),
        m_source(std::move(source))
    {}

    [[nodiscard]] virtual std::vector<Parameter>& getEditableParameters() = 0;

private:
    NomadId registerCapture(const Capture& capture) {
        const auto& name = capture.name;
        const auto existingCaptureId = getCaptureId(name);
        if (existingCaptureId != NOMAD_INVALID_ID) {
            const auto& existing = m_captures[toNomadIndex(existingCaptureId)];
            if (existing.source != capture.source) {
                throw NomadBug("Internal error: Capture '" + name + "' has a different source than expected");
            }
            const auto sameId = capture.source == CaptureSource::Variable ?
                existing.variableId == capture.variableId : existing.parameterId == capture.parameterId;
            if (!sameId) {
                throw NomadBug("Internal error: Capture '" + name + "' has a different id than expected");
            }
            if (existing.type != capture.type) {
                throw NomadBug("Internal error: Capture '" + name + "' has a different type than expected");
            }
            return existingCaptureId;
        }
        if (getVariableId(name).isValid()) {
            throw NomadBug("Internal error: Capture '" + name + "' is already registered as a variable");
        }
        if (getParameterId(name) != NOMAD_INVALID_ID) {
            throw NomadBug("Internal error: Capture '" + name + "' is already registered as a parameter");
        }
        m_captures.push_back(capture);
        addParameter(name, capture.type, "");
        return toNomadId(m_captures.size() - 1);
    }

    NomadString m_path;
    NomadString m_source;
    NomadString m_hub;
    NomadIndex m_sourceStartLine = NOMAD_INVALID_INDEX;
    NomadIndex m_sourceEndLine = NOMAD_INVALID_INDEX;
    NomadIndex m_functionStartIndex = NOMAD_INVALID_INDEX;
    NomadIndex m_functionEndIndex = NOMAD_INVALID_INDEX;
    VariableMap m_variables;
    std::vector<Capture> m_captures;
    CallableId m_parentId = CallableId{};
};

} // namespace nomad
