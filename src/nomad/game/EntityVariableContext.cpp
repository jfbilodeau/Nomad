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

NomadId ThisEntityVariableContext::getFirstVariableId() const {
    if (m_thisEntityVariableMap.getVariableCount() > 0) {
        return 0;
    }

    return NOMAD_INVALID_ID;
}

NomadId ThisEntityVariableContext::getNextVariableId(const NomadId variableId) const {
    if (variableId + 1 < static_cast<NomadId>(getVariableCount())) {
        return variableId + 1;
    }

    return NOMAD_INVALID_ID;
}

NomadId ThisEntityVariableContext::registerVariable(const NomadString &name, const Type *type) {
    NomadString this_name, other_name;

    if (name.starts_with(THIS_ENTITY_VARIABLE_PREFIX)) {
        this_name = name;
        other_name = OTHER_ENTITY_VARIABLE_PREFIX + name.substr(THIS_ENTITY_VARIABLE_PREFIX.size());
    } else if (name.starts_with(OTHER_ENTITY_VARIABLE_PREFIX)) {
        this_name = THIS_ENTITY_VARIABLE_PREFIX + name.substr(OTHER_ENTITY_VARIABLE_PREFIX.size());
        other_name = name;
    } else {
        log::error("Unexpected variable name '" + name + "'");
        return NOMAD_INVALID_ID;
    }

    auto this_id = m_thisEntityVariableMap.registerVariable(this_name, type);
    auto other_id = m_otherEntityVariableMap.registerVariable(other_name, type);

    // Sanity check...
    if (this_id != other_id) {
        log::error("Variable IDs do not match");
    }

    return this_id;
}

const NomadString & ThisEntityVariableContext::getVariableName(const NomadId variableId) const {
    return m_thisEntityVariableMap.getVariableName(variableId);
}

NomadId ThisEntityVariableContext::getVariableId(const NomadString &name) const {
    if (name.starts_with(THIS_ENTITY_VARIABLE_PREFIX)) {
        return m_thisEntityVariableMap.getVariableId(name);
    } else if (name.starts_with(OTHER_ENTITY_VARIABLE_PREFIX)) {
        return m_otherEntityVariableMap.getVariableId(name);
    } else {
        log::error("Unexpected variable name '" + name + "'");
        return NOMAD_INVALID_ID;
    }
}

void ThisEntityVariableContext::setVariableType(NomadId variableId, const Type *type) {
    m_thisEntityVariableMap.setVariableType(variableId, type);
    m_otherEntityVariableMap.setVariableType(variableId, type);
}

const Type* ThisEntityVariableContext::getVariableType(const NomadId variableId) const {
    return m_thisEntityVariableMap.getVariableType(variableId);
}

void ThisEntityVariableContext::setValue(const NomadId variableId, const RuntimeValue& value) {
    const auto entity = m_game->getCurrentContext()->getThisEntity();

    if (entity) {
        entity->setVariableValue(variableId, value);
    } else {
        log::error("EntityVariableContext::set_value: No entity in current context");
    }
}

void ThisEntityVariableContext::getValue(const NomadId variableId, RuntimeValue& value) {
    const auto entity = m_game->getCurrentContext()->getThisEntity();

    if (entity) {
        entity->getVariableValue(variableId, value);
    } else {
       log::error("EntityVariableContext::set_value: No entity in current context");
    }
}

bool ThisEntityVariableContext::isWritten(const NomadId variableId) const {
    return m_thisEntityVariableMap.isWritten(variableId);
}

void ThisEntityVariableContext::setWritten(const NomadId variableId, const bool written) {
    m_thisEntityVariableMap.setWritten(variableId, written);
    m_otherEntityVariableMap.setWritten(variableId, written);
}

bool ThisEntityVariableContext::isRead(const NomadId variableId) const {
    return m_thisEntityVariableMap.isRead(variableId);
}

void ThisEntityVariableContext::setRead(const NomadId variableId, const bool read) {
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

NomadId OtherEntityVariableContext::getFirstVariableId() const {
    return m_thisEntityVariableContext->getFirstVariableId();
}

NomadId OtherEntityVariableContext::getNextVariableId(const NomadId variableId) const {
    return m_thisEntityVariableContext->getNextVariableId(variableId);
}

NomadId OtherEntityVariableContext::registerVariable(const NomadString &name, const Type *type) {
    return m_thisEntityVariableContext->registerVariable(name, type);
}

const NomadString & OtherEntityVariableContext::getVariableName(NomadId variableId) const {
    return m_thisEntityVariableContext->getOtherVariableMap()->getVariableName(variableId);
}

NomadId OtherEntityVariableContext::getVariableId(const NomadString &name) const {
    return m_thisEntityVariableContext->getOtherVariableMap()->getVariableId(name);
}

void OtherEntityVariableContext::setVariableType(NomadId variableId, const Type *type) {
    return m_thisEntityVariableContext->setVariableType(variableId, type);
}

const Type* OtherEntityVariableContext::getVariableType(NomadId variableId) const {
    return m_thisEntityVariableContext->getVariableType(variableId);
}

void OtherEntityVariableContext::setValue(NomadId variableId, const RuntimeValue &value) {
    m_thisEntityVariableContext->getGame()->getCurrentContext()->forEachOtherEntities([&](Entity* entity) {
        entity->setVariableValue(variableId, value);
    });
}

void OtherEntityVariableContext::getValue(const NomadId variableId, RuntimeValue &value) {
    const auto entity = m_thisEntityVariableContext->getGame()->getCurrentContext()->getFirstOtherEntity();

    if (entity) {
        entity->getVariableValue(variableId, value);
    }
}

bool OtherEntityVariableContext::isWritten(const NomadId variableId) const {
    return m_thisEntityVariableContext->isWritten(variableId);
}

void OtherEntityVariableContext::setWritten(const NomadId variableId, const bool written) {
    m_thisEntityVariableContext->setWritten(variableId, written);
}

bool OtherEntityVariableContext::isRead(const NomadId variableId) const {
    return m_thisEntityVariableContext->isRead(variableId);
}

void OtherEntityVariableContext::setRead(const NomadId variableId, const bool read) {
    m_thisEntityVariableContext->setRead(variableId, read);
}

} // nomad
