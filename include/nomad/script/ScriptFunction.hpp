// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#ifndef NOMAD_FUNCTION_HPP
#define NOMAD_FUNCTION_HPP

#include <nomad/Nomad.hpp>

#include <nomad/script/Variable.hpp>

#include <vector>

namespace nomad {

enum class CaptureSource {
    Variable,
    Parameter,
};

struct Capture {
    NomadString name;
    NomadId id = NOMAD_INVALID_ID;
    const Type* type = nullptr;
    CaptureSource source = CaptureSource::Variable;
};

class Function {
public:
    Function(
        NomadId id,
        NomadString name,
        NomadString path,
        NomadString source
    );

    Function(const Function& other) = delete;

    ~Function();

    [[nodiscard]]
    NomadId getId() const { return m_id; }
    [[nodiscard]]
    const NomadString& getName() const { return m_name; }
    [[nodiscard]]
    const NomadString& getPath() const { return m_path; }
    [[nodiscard]]
    const NomadString& getSource() const { return m_source; }

    // Parameters
    NomadId addParameter(const NomadString& parameterName, const Type* type);
    [[nodiscard]]
    NomadId getParameterId(const NomadString& parameterName) const;
    [[nodiscard]]
    const NomadString& getParameterName(NomadId parameterId) const;
    [[nodiscard]]
    const Type* getParameterType(NomadId parameterId) const;
    [[nodiscard]]
    NomadIndex getParameterCount() const;

    // Capture tracking: flag when a parameter/variable is captured by nested function
    NomadId createCapture(NomadId id, const NomadString& name, CaptureSource source, const Type* type);
    NomadId flagParameterAsCaptured(NomadId parameterId, const NomadString& parameterName, const Type* parameterType);
    NomadId flagVariableAsCaptured(NomadId variableId, const NomadString& variableName, const Type* variableType);
    [[nodiscard]]
    NomadId getCaptureId(const NomadString& name) const;
    [[nodiscard]]
    NomadId getCaptureParameterId(NomadId captureId) const;
    [[nodiscard]]
    const Type* getCaptureType(NomadId captureId) const;

    [[nodiscard]]
    bool isCaptured(NomadId id, CaptureSource source) const;
    [[nodiscard]]
    NomadIndex getCaptureCount() const;
    [[nodiscard]]
    const std::vector<Capture>& getCaptures() const;

    // Parent function tracking (for nested functions/closures)
    void setParentId(NomadId parentId);
    [[nodiscard]]
    NomadId getParentId() const;
    [[nodiscard]]
    NomadIndex isNested() const;

    // Function variables.
    NomadId registerVariable(const NomadString& variableName, const Type* type);
    [[nodiscard]]
    NomadId getVariableId(const NomadString& variableName) const;
    [[nodiscard]]
    const NomadString& getVariableName(NomadId variableId) const;
    void setVariableType(NomadId variableId, const Type* type);
    [[nodiscard]]
    const Type* getVariableType(NomadId variableId) const;
    [[nodiscard]]
    NomadIndex getVariableCount() const;

    // Return type
    void setReturnType(const Type* returnType);
    [[nodiscard]]
    const Type* getReturnType() const;

    void setFunctionStart(NomadIndex functionStartIndex);
    [[nodiscard]]
    NomadIndex getFunctionStart() const;

    void setFunctionEnd(NomadIndex functionEndIndex);
    [[nodiscard]]
    NomadIndex getFunctionEnd() const;

    [[nodiscard]]
    NomadIndex getFunctionLength() const;

private:
    NomadId m_id;

    NomadString m_name;
    NomadString m_path;
    NomadString m_source;
    NomadIndex m_functionStartIndex = NOMAD_INVALID_INDEX;
    NomadIndex m_functionEndIndex = NOMAD_INVALID_INDEX;

    VariableMap m_variables;  // Local function variables (refactored from vector<VariableMetadata>)
    VariableMap m_parameters;

    // Capture tracking: which of MY parameters/variables are being captured by nested functions
    std::vector<Capture> m_captures;

    NomadId m_parentId = NOMAD_INVALID_ID;
    const Type* m_returnType = nullptr;
};

} // nomad

#endif //NOMAD_FUNCTION_HPP
