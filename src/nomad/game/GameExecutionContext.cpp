// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/GameExecutionContext.hpp>

#include <nomad/system/TempHeap.hpp>

namespace nomad {

void GameExecutionContext::reset(Scene* scene, Entity* entity) {
    m_scene = scene;
    m_entity = entity;
    clearOtherEntities();
}

void GameExecutionContext::setScene(Scene* scene) {
    m_scene = scene;
}

Scene* GameExecutionContext::getScene() const {
    return m_scene;
}

void GameExecutionContext::setThisEntity(Entity* entity) {
    m_entity = entity;
}

Entity* GameExecutionContext::getThisEntity() const {
    return m_entity;
}

void GameExecutionContext::pushOtherEntity(Entity* entity) {
    m_otherEntities.push_back(entity);
}

NomadIndex GameExecutionContext::getOtherEntityCount() const {
    return m_otherEntities.size();
}

void GameExecutionContext::clearOtherEntities() {
    m_otherEntities.clear();
}

void GameExecutionContext::addOtherEntity(Entity* entity) {
    m_otherEntities.push_back(entity);
}

void GameExecutionContext::clearOtherEntitiesAndAdd(Entity* entity) {
    m_otherEntities.clear();
    m_otherEntities.push_back(entity);
}

void GameExecutionContext::setOtherEntities(const std::vector<Entity*>& entities) {
    m_otherEntities = entities;
}

void GameExecutionContext::setOtherEntities(const TempVector<Entity*>& entities) {
    m_otherEntities.assign(entities.begin(), entities.end());
}

const std::vector<Entity*>& GameExecutionContext::getOtherEntities() const {
    return m_otherEntities;
}

Entity * GameExecutionContext::getFirstOtherEntity() const {
    if (m_otherEntities.empty()) {
        return nullptr;
    }

    return m_otherEntities.front();
}
} // nomad
