// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/EntityVariableContext.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Game.hpp>

namespace nomad {
ThisEntityVariableContext::ThisEntityVariableContext(Game* game):
    m_game(game)
{
}

NomadIndex ThisEntityVariableContext::getVariableCount() const {
    return m_thisEntityVariableMap.getVariableCount();
}

VariableId ThisEntityVariableContext::getFirstVariableId() const {
    if (m_thisEntityVariableMap.getVariableCount() > 0) {
        return VariableId{0};
    }

    return VariableId::INVALID;
}

VariableId ThisEntityVariableContext::getNextVariableId(const VariableId variableId) const {
    if (variableId.id + 1 < static_cast<NomadId>(getVariableCount())) {
        return VariableId{variableId.id + 1};
    }

    return VariableId::INVALID;
}

VariableId ThisEntityVariableContext::registerVariable(const NomadString &name, const Type *type) {
    NomadString this_name, other_name;

    if (name.starts_with(THIS_ENTITY_VARIABLE_PREFIX)) {
        this_name = name;
        other_name = OTHER_ENTITY_VARIABLE_PREFIX + name.substr(THIS_ENTITY_VARIABLE_PREFIX.size());
    } else if (name.starts_with(OTHER_ENTITY_VARIABLE_PREFIX)) {
        this_name = THIS_ENTITY_VARIABLE_PREFIX + name.substr(OTHER_ENTITY_VARIABLE_PREFIX.size());
        other_name = name;
    } else {
        log::error("Unexpected variable name '" + name + "'");
        return VariableId::INVALID;
    }

    auto this_id = m_thisEntityVariableMap.registerVariable(this_name, type);
    auto other_id = m_otherEntityVariableMap.registerVariable(other_name, type);

    // Sanity check...
    if (this_id != other_id) {
        log::error("Variable IDs do not match");
    }

    return this_id;
}

const NomadString & ThisEntityVariableContext::getVariableName(const VariableId variableId) const {
    return m_thisEntityVariableMap.getVariableName(variableId);
}

VariableId ThisEntityVariableContext::getVariableId(const NomadString &name) const {
    if (name.starts_with(THIS_ENTITY_VARIABLE_PREFIX)) {
        return m_thisEntityVariableMap.getVariableId(name);
    } else if (name.starts_with(OTHER_ENTITY_VARIABLE_PREFIX)) {
        return m_otherEntityVariableMap.getVariableId(name);
    } else {
        log::error("Unexpected variable name '" + name + "'");
        return VariableId::INVALID;
    }
}

void ThisEntityVariableContext::setVariableType(VariableId variableId, const Type *type) {
    m_thisEntityVariableMap.setVariableType(variableId, type);
    m_otherEntityVariableMap.setVariableType(variableId, type);
}

const Type* ThisEntityVariableContext::getVariableType(const VariableId variableId) const {
    return m_thisEntityVariableMap.getVariableType(variableId);
}

void ThisEntityVariableContext::setValue(const VariableId variableId, const RuntimeValue& value) {
    const auto entity = m_game->getCurrentContext()->getThisEntity();

    if (entity) {
        entity->setVariableValue(variableId, value);
    } else {
        log::error("EntityVariableContext::set_value: No entity in current context");
    }
}

void ThisEntityVariableContext::getValue(const VariableId variableId, RuntimeValue& value) {
    const auto entity = m_game->getCurrentContext()->getThisEntity();

    if (entity) {
        entity->getVariableValue(variableId, value);
    } else {
       log::error("EntityVariableContext::set_value: No entity in current context");
    }
}

bool ThisEntityVariableContext::isWritten(const VariableId variableId) const {
    return m_thisEntityVariableMap.isWritten(variableId);
}

void ThisEntityVariableContext::setWritten(const VariableId variableId, const bool written) {
    m_thisEntityVariableMap.setWritten(variableId, written);
    m_otherEntityVariableMap.setWritten(variableId, written);
}

bool ThisEntityVariableContext::isRead(const VariableId variableId) const {
    return m_thisEntityVariableMap.isRead(variableId);
}

void ThisEntityVariableContext::setRead(const VariableId variableId, const bool read) {
    m_thisEntityVariableMap.setRead(variableId, read);
    m_otherEntityVariableMap.setRead(variableId, read);
}

Game * ThisEntityVariableContext::getGame() const {
    return m_game;
}

const VariableMap* ThisEntityVariableContext::getThisVariableMap() const {
    return &m_thisEntityVariableMap;
}

const VariableMap * ThisEntityVariableContext::getOtherVariableMap() const {
    return &m_otherEntityVariableMap;
}

OtherEntityVariableContext::OtherEntityVariableContext(ThisEntityVariableContext *thisEntityVariableContext):
    m_thisEntityVariableContext(thisEntityVariableContext)
{
}

NomadIndex OtherEntityVariableContext::getVariableCount() const {
    return m_thisEntityVariableContext->getVariableCount();
}

VariableId OtherEntityVariableContext::getFirstVariableId() const {
    return m_thisEntityVariableContext->getFirstVariableId();
}

VariableId OtherEntityVariableContext::getNextVariableId(const VariableId variableId) const {
    return m_thisEntityVariableContext->getNextVariableId(variableId);
}

VariableId OtherEntityVariableContext::registerVariable(const NomadString &name, const Type *type) {
    return m_thisEntityVariableContext->registerVariable(name, type);
}

const NomadString & OtherEntityVariableContext::getVariableName(VariableId variableId) const {
    return m_thisEntityVariableContext->getOtherVariableMap()->getVariableName(variableId);
}

VariableId OtherEntityVariableContext::getVariableId(const NomadString &name) const {
    return m_thisEntityVariableContext->getOtherVariableMap()->getVariableId(name);
}

void OtherEntityVariableContext::setVariableType(VariableId variableId, const Type *type) {
    return m_thisEntityVariableContext->setVariableType(variableId, type);
}

const Type* OtherEntityVariableContext::getVariableType(VariableId variableId) const {
    return m_thisEntityVariableContext->getVariableType(variableId);
}

void OtherEntityVariableContext::setValue(VariableId variableId, const RuntimeValue &value) {
    m_thisEntityVariableContext->getGame()->getCurrentContext()->forEachOtherEntities([&](Entity* entity) {
        entity->setVariableValue(variableId, value);
    });
}

void OtherEntityVariableContext::getValue(const VariableId variableId, RuntimeValue &value) {
    const auto entity = m_thisEntityVariableContext->getGame()->getCurrentContext()->getFirstOtherEntity();

    if (entity) {
        entity->getVariableValue(variableId, value);
    }
}

bool OtherEntityVariableContext::isWritten(const VariableId variableId) const {
    return m_thisEntityVariableContext->isWritten(variableId);
}

void OtherEntityVariableContext::setWritten(const VariableId variableId, const bool written) {
    m_thisEntityVariableContext->setWritten(variableId, written);
}

bool OtherEntityVariableContext::isRead(const VariableId variableId) const {
    return m_thisEntityVariableContext->isRead(variableId);
}

void OtherEntityVariableContext::setRead(const VariableId variableId, const bool read) {
    m_thisEntityVariableContext->setRead(variableId, read);
}

} // nomad
