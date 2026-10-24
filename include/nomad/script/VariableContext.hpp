// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#ifndef NOMAD_VARIABLECONTEXT_HPP
#define NOMAD_VARIABLECONTEXT_HPP

#include <nomad/Nomad.hpp>

#include <nomad/script/Variable.hpp>

namespace nomad {

DeclareId(VariableContextId);
DeclareId(ContextVariableId);

class Type;

class VariableContext {
public:
    virtual ~VariableContext() = default;

    [[nodiscard]] virtual NomadIndex getVariableCount() const = 0;
    [[nodiscard]] virtual VariableId getFirstVariableId() const = 0;
    [[nodiscard]] virtual VariableId getNextVariableId(VariableId variableId) const = 0;

    virtual VariableId registerVariable(const NomadString& name, const Type* type) = 0;
    [[nodiscard]]
    virtual const NomadString& getVariableName(VariableId variableId) const = 0;

    [[nodiscard]]
    virtual VariableId getVariableId(const NomadString& name) const = 0;
    virtual void setVariableType(VariableId variableId, const Type* type) = 0;
    [[nodiscard]]
    virtual const Type* getVariableType(VariableId variableId) const = 0;

    virtual void setValue(VariableId variableId, const RuntimeValue& value) = 0;
    virtual void getValue(VariableId variableId, RuntimeValue& value) = 0;

    [[nodiscard]]
    virtual bool isWritten(VariableId variableId) const = 0;
    virtual void setWritten(VariableId variableId, bool written) = 0;
    [[nodiscard]]
    virtual bool isRead(VariableId variableId) const = 0;
    virtual void setRead(VariableId variableId, bool read) = 0;
};

class SimpleVariableContext : public VariableContext {
public:
    SimpleVariableContext();
    SimpleVariableContext(const SimpleVariableContext&) = delete;
    ~SimpleVariableContext() override;

    [[nodiscard]] NomadIndex getVariableCount() const override;
    [[nodiscard]] VariableId getFirstVariableId() const override;
    [[nodiscard]] VariableId getNextVariableId(VariableId variableId) const override;

    VariableId registerVariable(const NomadString& name, const Type* type) override;
    [[nodiscard]]
    const NomadString& getVariableName(VariableId variableId) const override;

    [[nodiscard]]
    VariableId getVariableId(const NomadString& name) const override;
    void setVariableType(VariableId variableId, const Type* type) override;
    [[nodiscard]]
    const Type* getVariableType(VariableId variableId) const override;

    void setValue(VariableId variableId, const RuntimeValue& value) override;
    void getValue(VariableId variableId, RuntimeValue& value) override;

    [[nodiscard]]
    bool isWritten(VariableId variableId) const override;
    void setWritten(VariableId variableId, bool written) override;
    [[nodiscard]]
    bool isRead(VariableId variableId) const override;
    void setRead(VariableId variableId, bool read) override;

    [[nodiscard]]
    const VariableMap* getVariableMap() const;

private:
    VariableMap m_variables;
    std::vector<RuntimeValue> m_values;
};

} // namespace nomad

#endif // NOMAD_VARIABLECONTEXT_HPP
