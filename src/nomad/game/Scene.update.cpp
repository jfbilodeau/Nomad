// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Scene.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/resource/ResourceManager.hpp>

#include <nomad/script/Runtime.hpp>

#include <box2d/box2d.h>

#include <algorithm>

namespace nomad {

static inline bool shapeIdsEqual(const b2ShapeId& a, const b2ShapeId& b) {
    return B2_ID_EQUALS(a, b);
}

// Scene tick update
void Scene::update(Game* game) {
    const auto functionId = getOnUpdateCallback();

    if (functionId != NOMAD_INVALID_ID) {
        game->executeFunction(functionId, this, nullptr);
    }

    for (const auto& entity : m_entities) {
        if (!entity->isPaused()) {
            entity->update(this);
        }
    }

    // Store previous position of entities.
    for (const auto& entity : m_entities) {
        entity->setPreviousPosition(entity->getPosition());
    }

    // Update layers physics
    if (m_physicsEnabled) {
        updatePhysics();
    }

    // Update camera
    updateCamera();

    // Update scheduled events
    updateScheduledEvents();

    // Make sure entities are in the right layer
    updateEntityLayers();

    // Sort entities by 'z'
    for (auto& layer : m_layers) {
        std::ranges::sort(layer.entities, [](auto a, auto b) {
            return a->getZ() < b->getZ();
        });
    }

    // Clear 'pressed' and 'released' flags on action mapping
    for (auto& map : m_actionMapping) {
        map.pressed = false;
        map.released = false;
    }

    // Execute post-update event
    const auto postUpdateFunctionId = getOnPostUpdateCallback();

    if (postUpdateFunctionId != NOMAD_INVALID_ID) {
        game->executeFunction(postUpdateFunctionId, this, nullptr);
    }

    // Increment frame number
    ++m_frameNumber;
}

void Scene::updateScheduledEvents() {
    const auto currentFrame = getFrameNumber();
    auto it = m_scheduledEvents.begin();

    while (it != m_scheduledEvents.end() && it->frameNumber <= currentFrame) {
        triggerUserEvent(it->name);

        it = m_scheduledEvents.erase(it);
    }
}

void Scene::generateLayerPhysicsBodies(Scene::Layer& layer) const {
    // Destroy existing walls.
    for (const auto& wall : layer.bodies) {
        b2DestroyBody(wall);
    }

    layer.bodies.clear();

    // Auxiliary vector to track processed tiles
    auto processed = createTempVector<bool>(m_tileMapHeight * m_tileMapWidth, false);
    // std::vector<bool> processed(m_tileMapHeight * m_tileMapWidth, false);

    for (auto y = 0; y < m_tileMapHeight; ++y) {
        for (auto x = 0; x < m_tileMapWidth; ++x) {
            const auto currentMask = getTileMask(layer.number, x, y);;

            if (currentMask == 0 || processed[y * m_tileMapWidth + x]) {
                continue; // Skip empty or already processed tiles
            }

            // Determine the bounds of the rectangle
            const int startX = x, startY = y;
            int endX = x, endY = y;

            // Expand horizontally
            while (
                endX + 1 < m_tileMapWidth &&
                getTileMask(layer.number, endX + 1, y) == currentMask &&
                !processed[y * m_tileMapWidth + (endX + 1)]
            ) {
                ++endX;
            }

            // Expand vertically
            bool canExpandVertically = true;
            while (canExpandVertically && endY + 1 < m_tileMapHeight) {
                for (int i = startX; i <= endX; ++i) {
                    if (
                        getTileMask(layer.number, i, endY + 1) != currentMask ||
                        processed[(endY + 1) * m_tileMapWidth + i]
                    ) {
                        canExpandVertically = false;
                        break;
                    }
                }
                if (canExpandVertically) {
                    ++endY;
                }
            }

            // Mark tiles as processed
            for (int i = startY; i <= endY; ++i) {
                for (int j = startX; j <= endX; ++j) {
                    processed[i * m_tileMapWidth + j] = true;
                }
            }

            // Create the physics body for the rectangle
            const auto x1 = startX * m_tileWidth;
            const auto x2 = (endX + 1) * m_tileWidth;
            const auto y1 = startY * m_tileHeight;
            const auto y2 = (endY + 1) * m_tileHeight;

            auto bodyDef = b2DefaultBodyDef();
            bodyDef.type = b2_staticBody;
            bodyDef.position = b2Vec2{
                static_cast<float>(x1 + x2) / 2.0f,
                static_cast<float>(y1 + y2) / 2.0f
            };

            auto bodyId = b2CreateBody(layer.worldId, &bodyDef);

            b2Polygon rectangle = b2MakeBox(
                static_cast<float>(x2 - x1) / 2.0f,
                static_cast<float>(y2 - y1) / 2.0f
            );

            b2ShapeDef shapeDef = b2DefaultShapeDef();
            shapeDef.enableSensorEvents = true;
            shapeDef.filter = b2Filter {
                static_cast<unsigned long long>(currentMask),
                std::numeric_limits<uint64_t>::max(),
                0,
            };
            b2CreatePolygonShape(bodyId, &shapeDef, &rectangle);

            layer.bodies.push_back(bodyId);
        }
    }
}

void Scene::updatePhysics() {
    auto timeStep = 1.0f / static_cast<float>(m_game->getFps());

    for (auto& layer : m_layers) {
        // Check for invalidated walls.
        if (layer.bodiesInvalidated) {
            layer.bodiesInvalidated = false;

            generateLayerPhysicsBodies(layer);
        }

        auto worldId = layer.worldId;

        for (auto entity : layer.entities) {
            entity->beforeSimulationUpdate(worldId);
        }

        b2World_Step(worldId, timeStep, 4);

        auto sensorEvents = b2World_GetSensorEvents(worldId);

        for (auto i = 0; i < sensorEvents.beginCount; ++i) {
            auto& [shapeIdSensor, shapeIdVisitor] = sensorEvents.beginEvents[i];

            auto bodyIdSensor = b2Shape_GetBody(shapeIdSensor);
            auto bodyIdVisitor = b2Shape_GetBody(shapeIdVisitor);

            auto entitySensor = static_cast<Entity*>(b2Body_GetUserData(bodyIdSensor));
            auto entityVisitor = static_cast<Entity*>(b2Body_GetUserData(bodyIdVisitor));

            NomadInteger visitorMask = entityVisitor ? entityVisitor->getMask() : b2Shape_GetFilter(shapeIdVisitor).categoryBits;

            // Store collision pair for later use when collision ends
            layer.activeSensorCollisions.push_back({
                shapeIdSensor,
                shapeIdVisitor,
                entitySensor,
                entityVisitor,
                visitorMask
            });

            entitySensor->triggerBeginCollision(entityVisitor, visitorMask);
        }

        for (auto i = 0; i < sensorEvents.endCount; ++i) {
            auto [shapeIdSensor, shapeIdVisitor] = sensorEvents.endEvents[i];

            Entity* entitySensor = nullptr;
            Entity* entityVisitor = nullptr;
            NomadInteger visitorMask = 0;

            // Try to find the collision pair in our tracked collisions
            auto it = std::ranges::find_if(
                layer.activeSensorCollisions,
                [&](const SensorCollisionPair& pair) {
                    return shapeIdsEqual(pair.sensorShapeId, shapeIdSensor) &&
                        shapeIdsEqual(pair.visitorShapeId, shapeIdVisitor);
                }
            );

            if (it != layer.activeSensorCollisions.end()) {
                // Found the collision pair - use stored entity data
                entitySensor = it->sensorEntity;
                entityVisitor = it->visitorEntity;
                visitorMask = it->visitorMask;

                // Remove from active collisions
                // Note: Using erase() is O(n) but acceptable for typical game scenarios
                // where active collision count is relatively small
                layer.activeSensorCollisions.erase(it);
            } else if (b2Shape_IsValid(shapeIdSensor) && b2Shape_IsValid(shapeIdVisitor)) {
                // Fallback: If shapes are valid, but we don't have the pair tracked,
                // retrieve the data from Box2D
                auto bodyIdSensor = b2Shape_GetBody(shapeIdSensor);
                auto bodyIdVisitor = b2Shape_GetBody(shapeIdVisitor);

                entitySensor = static_cast<Entity*>(b2Body_GetUserData(bodyIdSensor));
                entityVisitor = static_cast<Entity*>(b2Body_GetUserData(bodyIdVisitor));

                visitorMask = entityVisitor ? entityVisitor->getMask() : b2Shape_GetFilter(shapeIdVisitor).categoryBits;
            } else {
                // Shapes are invalid, and we don't have the pair tracked
                // This shouldn't normally happen, but skip if it does
                continue;
            }

            // Trigger the end collision event
            if (entitySensor) {
                entitySensor->triggerEndCollision(entityVisitor, visitorMask);
            }
        }

        // Process collisions
        auto contactEvents = b2World_GetContactEvents(worldId);

        for (auto i = 0; i < contactEvents.beginCount; ++i) {
            auto contactEvent = contactEvents.beginEvents[i];

            auto shapeIdA = contactEvent.shapeIdA;
            auto shapeIdB = contactEvent.shapeIdB;
            auto bodyIdA = b2Shape_GetBody(shapeIdA);
            auto bodyIdB = b2Shape_GetBody(shapeIdB);

            auto entityA = static_cast<Entity*>(b2Body_GetUserData(bodyIdA));
            auto entityB = static_cast<Entity*>(b2Body_GetUserData(bodyIdB));

            auto functionIdA = entityA ? entityA->getOnBeginCollision() : NOMAD_INVALID_ID;
            auto functionIdB = entityB ? entityB->getOnBeginCollision() : NOMAD_INVALID_ID;

            if (functionIdA != NOMAD_INVALID_ID && entityB) {
                m_game->executeFunction(functionIdA, this, entityA, entityB);
            }

            if (functionIdB != NOMAD_INVALID_ID && entityA) {
                m_game->executeFunction(functionIdB, this, entityB, entityA);
            }
        }

        for (auto i = 0; i < contactEvents.endCount; ++i) {
            auto contactEvent = contactEvents.endEvents[i];

            auto shapeIdA = contactEvent.shapeIdA;
            auto shapeIdB = contactEvent.shapeIdB;
            auto bodyIdA = b2Shape_GetBody(shapeIdA);
            auto bodyIdB = b2Shape_GetBody(shapeIdB);

            auto entityA = static_cast<Entity*>(b2Body_GetUserData(bodyIdA));
            auto entityB = static_cast<Entity*>(b2Body_GetUserData(bodyIdB));

            auto functionIdA = entityA ? entityA->getOnEndCollision() : NOMAD_INVALID_ID;
            auto functionIdB = entityB ? entityB->getOnEndCollision() : NOMAD_INVALID_ID;

            if (functionIdA != NOMAD_INVALID_ID) {
                if (entityB) {
                    m_game->executeFunction(functionIdA, this, entityA, entityB);
                } else {
                    m_game->executeFunction(functionIdA, this, entityA);
                }
            }

            if (functionIdB != NOMAD_INVALID_ID) {
                if (entityA) {
                    m_game->executeFunction(functionIdB, this, entityB, entityA);
                } else {
                    m_game->executeFunction(functionIdB, this, entityB);
                }
            }
        }

        for (auto entity : layer.entities) {
            entity->afterSimulationUpdate(worldId);
        }

        // If entity does not have a body, update the velocity manually.
        // for (auto entity : layer.entities) {
        //     auto bodyType = entity->getBodyType();
        //
        //     // if (bodyType == BodyType::Static) {
        //     //     entity->afterSimulationUpdate(worldId);
        //     // }
        // }
    }
}

void Scene::updateEntityLayers() {
    for (NomadIndex layerIndex = 0; layerIndex < m_layers.size(); ++layerIndex) {
        const auto layer = &m_layers[layerIndex];

        // If entity is not in the right layer, move it to its layer.
        NomadIndex entityIndex = 0;

        while (entityIndex < layer->entities.size()) {
            auto entity = layer->entities[entityIndex];
            auto entityLayer = entity->getLayer();
            if (entityLayer != static_cast<NomadInteger>(layerIndex)) {
                if (entityLayer < 0 || entityLayer >= static_cast<NomadInteger>(m_layers.size())) {
                    log::warning("Entity '" + entity->getName() + "'[" + toString(entity->getId()) + "] has an invalid layer : " + std::to_string(entityLayer) + ".Moving to layer 0");

                    entityLayer = 0;

                    entity->setLayer(entityLayer);

                    if (layerIndex == 0) {
                        // Already in the right layer
                        continue;
                    }
                }

                m_layers[entityLayer].entities.push_back(entity);
                layer->entities.erase(layer->entities.begin() + entityIndex);
            }
            else {
                // Entity is in the right layer, move to next entity.
                ++entityIndex;
            }
        }
    }
}

void Scene::updateCamera() {
    if (m_cameraFollowEntityId != NOMAD_INVALID_ID) {
        const auto entity = getEntityById(m_cameraFollowEntityId);

        if (entity) {
            m_cameraPosition.set(entity->getPosition());
        } else {
            // Entity no longer exists, stop following
            m_cameraFollowEntityId = NOMAD_INVALID_ID;
        }
    }

    // Update which entities are in the camera view
    const auto resolution = m_game->getResolution().toPointF();

    const auto cameraRectangle = RectangleF{
        m_cameraPosition.getX() - resolution.getX() / 2,
        m_cameraPosition.getY() - resolution.getY() / 2,
        resolution.getX(),
        resolution.getY()
    };

    for (const auto& entity : m_entities) {
        RectangleF boundingBox;

        entity->getBoundingBox(boundingBox);

        if (cameraRectangle.intersects(boundingBox)) {
            entity->enterCamera();
        } else {
            entity->exitCamera();
        }
    }
}

} // nomad
