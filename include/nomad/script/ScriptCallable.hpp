// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/log/Logger.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/Variable.hpp>

#include <concepts>
#include <utility>
#include <vector>

namespace nomad {

enum class CaptureSource {
    Variable = 1,
    Parameter,
};

struct Capture {
    NomadString name;
    NomadId id = NOMAD_INVALID_ID;
    const Type* type = nullptr;
    CaptureSource source = CaptureSource::Variable;
};

template<class Base>
    requires (std::same_as<Base, Function> || std::same_as<Base, Method> || std::same_as<Base, Constructor>)
class ScriptCallable : public Base {
public:
    [[nodiscard]] const NomadString& getPath() const { return m_path; }
    [[nodiscard]] const NomadString& getSourceCode() const { return m_source; }

    void setSourceLineRange(const NomadIndex startLine, const NomadIndex endLine) {
        m_sourceStartLine = startLine;
        m_sourceEndLine = endLine;
    }

    [[nodiscard]] NomadIndex getSourceStartLine() const { return m_sourceStartLine; }
    [[nodiscard]] NomadIndex getSourceEndLine() const { return m_sourceEndLine; }

    NomadId addParameter(const NomadString& parameterName, const Type* type) {
        auto parameterId = getParameterId(parameterName);
        auto& parameters = this->getEditableParameters();

        if (parameterId == NOMAD_INVALID_ID) {
            parameterId = toNomadId(parameters.size());
            parameters.push_back({parameterName, type});
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
        const NomadId id,
        const NomadString& name,
        const CaptureSource source,
        const Type* type
    ) {
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
        m_captures.push_back({name, id, type, source});
        addParameter(name, type);
        return toNomadId(m_captures.size() - 1);
    }

    NomadId flagParameterAsCaptured(
        const NomadId parameterId,
        const NomadString& parameterName,
        const Type* parameterType
    ) {
        return createCapture(parameterId, parameterName, CaptureSource::Parameter, parameterType);
    }

    NomadId flagVariableAsCaptured(
        const NomadId variableId,
        const NomadString& variableName,
        const Type* variableType
    ) {
        return createCapture(variableId, variableName, CaptureSource::Variable, variableType);
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

    [[nodiscard]] bool isCaptured(const NomadId id, const CaptureSource source) const {
        for (const auto& capture : m_captures) {
            if (capture.id == id && capture.source == source) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] NomadIndex getCaptureCount() const { return m_captures.size(); }
    [[nodiscard]] const std::vector<Capture>& getCaptures() const { return m_captures; }

    void setParentId(const NomadId parentId) { m_parentId = parentId; }
    [[nodiscard]] NomadId getParentId() const { return m_parentId; }
    [[nodiscard]] NomadIndex isNested() const { return m_parentId != NOMAD_INVALID_ID; }

    NomadId registerVariable(const NomadString& variableName, const Type* type) {
        const auto variableId = getVariableId(variableName);
        if (variableId != NOMAD_INVALID_ID) {
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

    [[nodiscard]] NomadId getVariableId(const NomadString& variableName) const {
        return m_variables.getVariableId(variableName);
    }

    [[nodiscard]] const NomadString& getVariableName(const NomadId variableId) const {
        return m_variables.getVariableName(variableId);
    }

    void setVariableType(const NomadId variableId, const Type* type) {
        m_variables.setVariableType(variableId, type);
    }

    [[nodiscard]] const Type* getVariableType(const NomadId variableId) const {
        return m_variables.getVariableType(variableId);
    }

    [[nodiscard]] NomadIndex getVariableCount() const { return m_variables.getVariableCount(); }

    using Base::setReturnType;

    void setFunctionStart(const NomadIndex functionStartIndex) { m_functionStartIndex = functionStartIndex; }
    [[nodiscard]] NomadIndex getFunctionStart() const { return m_functionStartIndex; }
    void setFunctionEnd(const NomadIndex functionEndIndex) { m_functionEndIndex = functionEndIndex; }
    [[nodiscard]] NomadIndex getFunctionEnd() const { return m_functionEndIndex; }
    [[nodiscard]] NomadIndex getFunctionLength() const { return m_functionEndIndex - m_functionStartIndex; }

protected:
    template<class... Arguments>
    ScriptCallable(
        const NomadId id,
        NomadString path,
        NomadString source,
        Arguments&&... arguments
    ):
        Base(CallableSource::Script, std::forward<Arguments>(arguments)...),
        m_path(std::move(path)),
        m_source(std::move(source))
    {
        this->setId(id);
    }

private:
    NomadString m_path;
    NomadString m_source;
    NomadIndex m_sourceStartLine = NOMAD_INVALID_INDEX;
    NomadIndex m_sourceEndLine = NOMAD_INVALID_INDEX;
    NomadIndex m_functionStartIndex = NOMAD_INVALID_INDEX;
    NomadIndex m_functionEndIndex = NOMAD_INVALID_INDEX;
    VariableMap m_variables;
    std::vector<Capture> m_captures;
    NomadId m_parentId = NOMAD_INVALID_ID;
};

class ScriptFunction : public ScriptCallable<Function> {
public:
    ScriptFunction(
        const NomadId id,
        NomadString name,
        NomadString path,
        NomadString source
    ):
        ScriptCallable(
            id,
            std::move(path),
            std::move(source),
            std::move(name),
            std::vector<Parameter>{},
            nullptr,
            NomadString{}
        )
    {
    }

    [[nodiscard]] ScriptFunction* asScriptFunction() override { return this; }
    [[nodiscard]] const ScriptFunction* asScriptFunction() const override { return this; }
};

} // namespace nomad
