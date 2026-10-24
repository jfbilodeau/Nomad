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
    VariableId getFirstVariableId() const override;
    VariableId getNextVariableId(VariableId variableId) const override;

    VariableId registerVariable(const NomadString &name, const Type* type) override;
    [[nodiscard]]
    const NomadString & getVariableName(VariableId variableId) const override;
    [[nodiscard]]
    VariableId getVariableId(const NomadString &name) const override;

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
    VariableId getFirstVariableId() const override;
    VariableId getNextVariableId(VariableId variableId) const override;

    VariableId registerVariable(const NomadString &name, const Type *type) override;
    [[nodiscard]]
    const NomadString & getVariableName(VariableId variableId) const override;
    [[nodiscard]]
    VariableId getVariableId(const NomadString &name) const override;

    void setVariableType(VariableId variableId, const Type *type) override;
    [[nodiscard]]
    const Type * getVariableType(VariableId variableId) const override;

    void setValue(VariableId variableId, const RuntimeValue& value) override;
    void getValue(VariableId variableId, RuntimeValue& value) override;

    [[nodiscard]]
    bool isWritten(VariableId variableId) const override;
    void setWritten(VariableId variableId, bool written) override;
    [[nodiscard]]
    bool isRead(VariableId variableId) const override;
    void setRead(VariableId variableId, bool read) override;

private:
    ThisEntityVariableContext* m_thisEntityVariableContext;
};

} // nomad
