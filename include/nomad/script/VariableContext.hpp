// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#ifndef NOMAD_VARIABLECONTEXT_HPP
#define NOMAD_VARIABLECONTEXT_HPP

#include <nomad/Nomad.hpp>

#include <nomad/script/Variable.hpp>

namespace nomad {

class Type;

class VariableContext {
public:
    virtual ~VariableContext() = default;

    [[nodiscard]] virtual NomadIndex getVariableCount() const = 0;
    [[nodiscard]] virtual NomadId getFirstVariableId() const = 0;
    [[nodiscard]] virtual NomadId getNextVariableId(NomadId variableId) const = 0;

    virtual NomadId registerVariable(const NomadString& name, const Type* type) = 0;
    [[nodiscard]]
    virtual const NomadString& getVariableName(NomadId variableId) const = 0;

    [[nodiscard]]
    virtual NomadId getVariableId(const NomadString& name) const = 0;
    virtual void setVariableType(NomadId variableId, const Type* type) = 0;
    [[nodiscard]]
    virtual const Type* getVariableType(NomadId variableId) const = 0;

    virtual void setValue(NomadId variableId, const RuntimeValue& value) = 0;
    virtual void getValue(NomadId variableId, RuntimeValue& value) = 0;

    [[nodiscard]]
    virtual bool isWritten(NomadId variableId) const = 0;
    virtual void setWritten(NomadId variableId, bool written) = 0;
    [[nodiscard]]
    virtual bool isRead(NomadId variableId) const = 0;
    virtual void setRead(NomadId variableId, bool read) = 0;
};

class SimpleVariableContext : public VariableContext {
public:
    SimpleVariableContext();
    SimpleVariableContext(const SimpleVariableContext&) = delete;
    ~SimpleVariableContext() override;

    [[nodiscard]] NomadIndex getVariableCount() const override;
    [[nodiscard]] NomadId getFirstVariableId() const override;
    [[nodiscard]] NomadId getNextVariableId(NomadId variableId) const override;

    NomadId registerVariable(const NomadString& name, const Type* type) override;
    [[nodiscard]]
    const NomadString& getVariableName(NomadId variableId) const override;

    [[nodiscard]]
    NomadId getVariableId(const NomadString& name) const override;
    void setVariableType(NomadId variableId, const Type* type) override;
    [[nodiscard]]
    const Type* getVariableType(NomadId variableId) const override;

    void setValue(NomadId variableId, const RuntimeValue& value) override;
    void getValue(NomadId variableId, RuntimeValue& value) override;

    [[nodiscard]]
    bool isWritten(NomadId variableId) const override;
    void setWritten(NomadId variableId, bool written) override;
    [[nodiscard]]
    bool isRead(NomadId variableId) const override;
    void setRead(NomadId variableId, bool read) override;

    [[nodiscard]]
    const VariableMap* getVariableMap() const;

private:
    VariableMap m_variables;
    std::vector<RuntimeValue> m_values;
};

} // namespace nomad

#endif // NOMAD_VARIABLECONTEXT_HPP
