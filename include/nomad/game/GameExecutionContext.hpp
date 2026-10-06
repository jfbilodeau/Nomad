// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/system/TempHeap.hpp>

#include <functional>

namespace nomad {

// Forward declarations
class Entity;
class Game;
class Scene;

class GameExecutionContext {
public:
    explicit GameExecutionContext() = default;
    GameExecutionContext(const GameExecutionContext&) = delete;
    ~GameExecutionContext() = default;

    void reset(Scene* scene, Entity* thisEntity);

    void setScene(Scene* scene);
    [[nodiscard]]
    Scene* getScene() const;

    void setThisEntity(Entity* entity);
    [[nodiscard]]
    Entity* getThisEntity() const;

    void pushOtherEntity(Entity* entity);
    [[nodiscard]]
    NomadIndex getOtherEntityCount() const;
    void clearOtherEntities();
    void addOtherEntity(Entity* entity);
    void clearOtherEntitiesAndAdd(Entity* entity);
    void setOtherEntities(const std::vector<Entity*>& entities);
    void setOtherEntities(const TempVector<Entity*>& entities);
    [[nodiscard]]
    const std::vector<Entity*>& getOtherEntities() const;

    Entity* getFirstOtherEntity() const;

    void forEachOtherEntities(const std::function<void(Entity*)>& callback) const {
        for (auto& entity : m_otherEntities) {
            callback(entity);
        }
    }

private:
    Scene* m_scene = nullptr;
    Entity* m_entity = nullptr;
    std::vector<Entity*> m_otherEntities;
};

} // nomad
