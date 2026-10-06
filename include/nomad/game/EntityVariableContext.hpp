// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/VariableContext.hpp>

namespace nomad {

const NomadString THIS_ENTITY_VARIABLE_CONTEXT = "this";
const NomadString OTHER_ENTITY_VARIABLE_CONTEXT = "other";
const NomadString THIS_ENTITY_VARIABLE_PREFIX = THIS_ENTITY_VARIABLE_CONTEXT + ".";
const NomadString OTHER_ENTITY_VARIABLE_PREFIX = OTHER_ENTITY_VARIABLE_CONTEXT + ".";

// Forward declarations
class Game;

class ThisEntityVariableContext final : public VariableContext {
public:
    explicit ThisEntityVariableContext(Game* game);
    ThisEntityVariableContext(const ThisEntityVariableContext&) = delete;
    ~ThisEntityVariableContext() override = default;

    [[nodiscard]] NomadIndex getVariableCount() const override;
    NomadId getFirstVariableId() const override;
    NomadId getNextVariableId(NomadId variableId) const override;

    NomadId registerVariable(const NomadString &name, const Type* type) override;
    [[nodiscard]]
    const NomadString & getVariableName(NomadId variableId) const override;
    [[nodiscard]]
    NomadId getVariableId(const NomadString &name) const override;

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
    Game* getGame() const;

    [[nodiscard]]
    const VariableMap* getThisVariableMap() const;
    [[nodiscard]]
    const VariableMap* getOtherVariableMap() const;

private:
    Game* m_game;
    VariableMap m_thisEntityVariableMap;
    VariableMap m_otherEntityVariableMap;
};

class OtherEntityVariableContext final : public VariableContext {
public:
    explicit OtherEntityVariableContext(ThisEntityVariableContext* thisEntityVariableContext);
    OtherEntityVariableContext(const OtherEntityVariableContext&) = delete;
    ~OtherEntityVariableContext() override = default;

    [[nodiscard]] NomadIndex getVariableCount() const override;
    NomadId getFirstVariableId() const override;
    NomadId getNextVariableId(NomadId variableId) const override;

    NomadId registerVariable(const NomadString &name, const Type *type) override;
    [[nodiscard]]
    const NomadString & getVariableName(NomadId variableId) const override;
    [[nodiscard]]
    NomadId getVariableId(const NomadString &name) const override;

    void setVariableType(NomadId variableId, const Type *type) override;
    [[nodiscard]]
    const Type * getVariableType(NomadId variableId) const override;

    void setValue(NomadId variableId, const RuntimeValue& value) override;
    void getValue(NomadId variableId, RuntimeValue& value) override;

    [[nodiscard]]
    bool isWritten(NomadId variableId) const override;
    void setWritten(NomadId variableId, bool written) override;
    [[nodiscard]]
    bool isRead(NomadId variableId) const override;
    void setRead(NomadId variableId, bool read) override;

private:
    ThisEntityVariableContext* m_thisEntityVariableContext;
};

} // nomad
