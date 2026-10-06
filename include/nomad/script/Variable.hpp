// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

class VariableDefinition {
public:
    VariableDefinition(const NomadString& name, const Type* type);

    [[nodiscard]] const NomadString& getName() const;
    void setType(const Type* type);
    [[nodiscard]] const Type* getType() const;

    [[nodiscard]]
    bool isWritten() const;
    void setWritten(bool written);
    [[nodiscard]]
    bool isRead() const;
    void setRead(bool read);

private:
    NomadString m_name;
    const Type* m_type;
    bool m_isWritten = false;
    bool m_isRead = false;
};

class VariableMap {
public:
    VariableMap() = default;

    NomadId registerVariable(const NomadString& name, const Type* type);
    [[nodiscard]] NomadId getVariableId(const NomadString& name) const;
    [[nodiscard]] const NomadString& getVariableName(NomadId variableId) const;
    void setVariableType(NomadId variableId, const Type* type);
    [[nodiscard]] const Type* getVariableType(NomadId variableId) const;
    [[nodiscard]] NomadIndex getVariableCount() const;

    [[nodiscard]]
    bool isWritten(NomadId variableId) const;
    void setWritten(NomadId variableId, bool written);
    [[nodiscard]]
    bool isRead(NomadId variableId) const;
    void setRead(NomadId variableId, bool read);

private:
    std::vector<VariableDefinition> m_variables;
};

class VariableList {
public:
    explicit VariableList(const VariableMap* variableMap);
    ~VariableList();

    [[nodiscard]] NomadId getVariableId(const NomadString& name) const;
    [[nodiscard]] const NomadString& getVariableName(NomadId variableId) const;
    [[nodiscard]] const Type* getVariableType(NomadId variableId) const;
    [[nodiscard]] NomadIndex getVariableCount() const;

    void setVariableValue(NomadId variableId, const RuntimeValue& value);
    void getVariableValue(NomadId variableId, RuntimeValue& value) const;

private:
    const VariableMap m_variableMap;
    std::vector<RuntimeValue> m_variableValues;
};

} // nomad
