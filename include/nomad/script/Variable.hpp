// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

DeclareId(VariableId);

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

    VariableId registerVariable(const NomadString& name, const Type* type);
    [[nodiscard]] VariableId getVariableId(const NomadString& name) const;
    [[nodiscard]] const NomadString& getVariableName(VariableId variableId) const;
    void setVariableType(VariableId variableId, const Type* type);
    [[nodiscard]] const Type* getVariableType(VariableId variableId) const;
    [[nodiscard]] NomadIndex getVariableCount() const;

    [[nodiscard]]
    bool isWritten(VariableId variableId) const;
    void setWritten(VariableId variableId, bool written);
    [[nodiscard]]
    bool isRead(VariableId variableId) const;
    void setRead(VariableId variableId, bool read);

private:
    std::vector<VariableDefinition> m_variables;
};

class VariableList {
public:
    explicit VariableList(const VariableMap* variableMap);
    ~VariableList();

    [[nodiscard]] VariableId getVariableId(const NomadString& name) const;
    [[nodiscard]] const NomadString& getVariableName(VariableId variableId) const;
    [[nodiscard]] const Type* getVariableType(VariableId variableId) const;
    [[nodiscard]] NomadIndex getVariableCount() const;

    void setVariableValue(VariableId variableId, const RuntimeValue& value);
    void getVariableValue(VariableId variableId, RuntimeValue& value) const;

private:
    const VariableMap m_variableMap;
    std::vector<RuntimeValue> m_variableValues;
};

} // nomad
