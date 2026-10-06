// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Scene.hpp>

#include <nomad/geometry/Rectangle.hpp>
#include <nomad/geometry/CircleF.hpp>

#include <nomad/resource/ResourceManager.hpp>
#include <nomad/resource/Texture.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/script/Runtime.hpp>

#include <algorithm>

namespace nomad {

Scene::Scene(Game* game, const NomadId sceneId):
    m_game(game),
    m_id (sceneId),
    m_variables(game->getSceneVariables())
{
    initializePhysicsLayers();
}

Scene::Scene(const NomadId sceneId, const VariableMap* variableMap):
    m_game(nullptr),
    m_id(sceneId),
    m_variables(variableMap)
{
    initializePhysicsLayers();
}

void Scene::initializePhysicsLayers() {
    // Create Box2D worlds
    for (NomadInteger layerNumber = 0; layerNumber < SCENE_LAYER_COUNT; ++layerNumber) {
        auto& layerDefinition = m_layers[static_cast<size_t>(layerNumber)];

        layerDefinition.number = layerNumber;

        b2WorldDef world_def = b2DefaultWorldDef();

        // No gravity
        world_def.gravity = { 0.0f, 0.0f };
        // world_def.hitEventThreshold = 0.001f;
        // world_def.enableSleep = false;
        // world_def.contactHertz = 30.0f;

        layerDefinition.worldId = b2CreateWorld(&world_def);
    }
}

Scene::~Scene() {
    while (!m_entities.empty()) {
        removeEntity(m_entities.back().get());
    }

    for (const auto& layer : m_layers) {
        b2DestroyWorld(layer.worldId);
    }
}

Game* Scene::getGame() const {
    return m_game;
}

NomadId Scene::getId() const {
    return m_id;
}

void Scene::setName(const NomadString& name) {
    m_name = name;
}

NomadString Scene::getName() const {
    return m_name;
}

void Scene::setZ(NomadInteger z) {
    this->m_z = z;
}

NomadInteger Scene::getZ() const {
    return m_z;
}

void Scene::setFrameNumber(NomadInteger frameNumber) {
    m_frameNumber = frameNumber;
}

NomadInteger Scene::getFrameNumber() const {
    return m_frameNumber;
}

void Scene::processInputEvent(const InputEvent& event) {
    NomadString action_name;

    if (m_actionManager.getActionNameForInput(event.code, action_name)) {
        for (auto& map: m_actionMapping) {
            if (
                map.name == action_name &&
                map.action == event.action
            ) {
                map.pressed = event.action == InputAction::Pressed;
                map.released = event.action == InputAction::Released;
                map.held = map.pressed;

                const auto entity = getEntityById(map.entityId);

                m_game->executeFunction(map.functionId, this, entity);
            }
        }
    }
}

void Scene::setVariableValue(NomadId variableId, const RuntimeValue& value) {
    m_variables.setVariableValue(variableId, value);
}

void Scene::getVariableValue(NomadId variableId, RuntimeValue& value) const {
    m_variables.getVariableValue(variableId, value);
}

void Scene::addEntity(std::unique_ptr<Entity> entity) {
    auto entityPtr = entity.get();
    m_entities.push_back(std::move(entity));

    auto entityLayer = entityPtr->getLayer();

    if (entityLayer < 0 || entityLayer >= static_cast<NomadInteger>(m_layers.size())) {
        log::warning("Entity '" + entityPtr->getName() + "' has an invalid layer id: " + std::to_string(entityLayer) + ". Defaulting to layer 0");

        entityLayer = 0;
    }

    entityPtr->setLayer(entityLayer);

    m_layers[static_cast<size_t>(entityLayer)].entities.push_back(entityPtr);
}

void Scene::removeEntity(Entity* entity) {
    // Remove all events associated with this entity
    auto entityId = entity->getId();
    unregisterEntityFromAllEvents(entityId);

    const auto [first, last] = std::ranges::remove_if(m_actionMapping,
    [entityId](const ActionMapping& mapping) {
           return mapping.entityId == entityId;
        }
    );

    // Remove physics
    entity->destroyBody();

    // Remove entity from collision tracking to prevent dangling pointers
    removeEntityFromCollisionTracking(entity);

    // Remove from layer.
    const auto entityLayer = entity->getLayer();
    if (entityLayer >= 0 && entityLayer < static_cast<NomadInteger>(m_layers.size())) {
        auto& layerEntities = m_layers[static_cast<size_t>(entityLayer)].entities;
        std::erase(layerEntities, entity);
    }


    m_actionMapping.erase(first, last);

    const auto ownedEntity = std::ranges::find_if(
        m_entities,
        [entity](const std::unique_ptr<Entity>& candidate) {
            return candidate.get() == entity;
        }
    );

    if (ownedEntity != m_entities.end()) {
        m_entities.erase(ownedEntity);
    }
}

Entity* Scene::getEntityById(const NomadId id) const {
    for (const auto& entity : m_entities) {
        if (entity->getId() == id) {
            return entity.get();
        }
    }

    return nullptr;
}

NomadId Scene::generateEntityId() {
    if (m_entities.size() >= NOMAD_ID_MAX - NOMAD_ID_MIN) {
        // We're already at the max number of entities.
        return NOMAD_INVALID_ID;
    }

    const auto startId = ++m_entityIdCounter;

    while (true) {
        const auto entity = getEntityById(m_entityIdCounter);

        if (entity == nullptr) {
            return m_entityIdCounter;
        }

        m_entityIdCounter++;

        if (m_entityIdCounter == NOMAD_ID_MAX) {
            m_entityIdCounter = NOMAD_ID_MIN;
        }

        if (m_entityIdCounter == startId) {
            return NOMAD_INVALID_ID;
        }
    }
}

Entity* Scene::getEntityByName(const NomadString& name) const {
    for (const auto& entity : m_entities) {
        if (entity->getName() == name) {
            return entity.get();
        }
    }

    return nullptr;
}

void Scene::getEntitiesByName(const NomadString& name, EntityList& entities) const {
    for (const auto& entity : m_entities) {
        if (entity->getName() == name) {
            entities.push_back(entity.get());
        }
    }
}

void Scene::pauseOtherEntities(Entity* entity) {
    for (const auto& other_entity : m_entities) {
        if (other_entity.get() != entity) {
            other_entity->pause();
        }
    }
}

void Scene::pauseOtherEntities(const std::vector<Entity*>& entities) {
    for (const auto& other_entity : m_entities) {
        if (std::find(entities.begin(), entities.end(), other_entity.get()) == entities.end()) {
            other_entity->pause();
        }
    }
}

void Scene::pauseAllEntities() {
    for (const auto& entity : m_entities) {
        entity->pause();
    }
}

void Scene::unpauseAllEntities() {
    for (const auto& entity : m_entities) {
        entity->unpause();
    }
}

void Scene::unpauseAllVisibleEntities() {
    for (const auto& entity : m_entities) {
        if (entity->isVisible()) {
            entity->unpause();
        }
    }
}

void Scene::loadActionMapping(const NomadString& mappingName) {
    m_actionManager.loadMapping(m_game, mappingName);
}

void Scene::saveActionMapping(const NomadString& mappingName) {
    m_actionManager.saveMapping(m_game, mappingName);
}

void Scene::resetActionMapping(const NomadString& mappingName) {
    m_actionManager.resetMappingToDefaults(m_game, mappingName);
}

void Scene::addActionPressed(const NomadString& actionName, NomadId functionId, NomadId entityId) {
    auto mapping = getActionMapping(actionName, InputAction::Pressed, entityId);

    if (mapping) {
        // Update mapping:
        mapping->functionId = functionId;
    } else {
        m_actionMapping.emplace_back(
            ActionMapping{
                actionName,
                InputAction::Pressed,
                functionId,
                entityId
            }
        );
    }
}

void Scene::addActionReleased(const NomadString& actionName, NomadId id, NomadId entityId) {
    auto mapping = getActionMapping(actionName, InputAction::Released, entityId);

    if (mapping) {
        // Update mapping:
        mapping->functionId = id;
    } else {
        m_actionMapping.emplace_back(
            ActionMapping{
                actionName,
                InputAction::Released,
                id,
                entityId
            }
        );
    }
}

void Scene::removeActionPressed(const NomadString& actionName, NomadId entityId) {
    const auto i = std::find_if(m_actionMapping.begin(), m_actionMapping.end(), [actionName, entityId](const ActionMapping& mapping) {
        return mapping.name == actionName && mapping.action == InputAction::Pressed && mapping.entityId == entityId;
    });

    m_actionMapping.erase(i);
}

void Scene::removeActionReleased(const NomadString& actionName, NomadId entityId) {
    const auto i = std::find_if(m_actionMapping.begin(), m_actionMapping.end(), [actionName, entityId](const ActionMapping& mapping) {
        return mapping.name == actionName && mapping.action == InputAction::Pressed && mapping.entityId == entityId;
    });

    m_actionMapping.erase(i);
}

Scene::ActionMapping* Scene::getActionMapping(const NomadString& name, InputAction type, NomadId entityId) {
    for (auto& mapping : m_actionMapping) {
        if (mapping.name == name && mapping.action == type && mapping.entityId == entityId) {
            return &mapping;
        }
    }

    return nullptr;
}

bool Scene::addAnimatedTile(const NomadString& tileName, const NomadInteger count, const NomadInteger speed) {
    const auto tile = std::ranges::find_if(m_tiles, [&tileName](const TileDefinition& definition) {
        return definition.name == tileName;
    });

    if (tileName.empty() || tile == m_tiles.end()) {
        return false;
    }

    const auto startId = static_cast<NomadInteger>(std::distance(m_tiles.begin(), tile));
    if (
        startId <= 0 ||
        count <= 0 ||
        speed <= 0 ||
        startId >= m_tileCount ||
        count > m_tileCount - startId
    ) {
        return false;
    }

    m_animatedTiles.push_back({
        static_cast<NomadIndex>(startId),
        static_cast<NomadIndex>(count),
        speed,
        m_frameNumber
    });

    return true;
}

NomadIndex AnimatedTileDefinition::resolveTileId(const NomadIndex tileId, const NomadInteger frameNumber) const {
    if (tileId != startId || frameNumber < startFrame) {
        return tileId;
    }

    const auto elapsedFrames = frameNumber - startFrame;
    const auto frameOffset = static_cast<NomadIndex>(elapsedFrames / speed) % count;
    return startId + frameOffset;
}

void Scene::initTileSet(
    const Texture* texture,
    const NomadInteger tileWidth,
    const NomadInteger tileHeight,
    const NomadInteger firstTileIndex,
    const std::vector<NomadInteger>& masks
) {
    if (texture == nullptr || tileWidth <= 0 || tileHeight <= 0) {
        log::error("Cannot initialize tileset with invalid texture or tile size");
        return;
    }

    m_tileTexture = texture;
    const auto horizontalTileCount = texture->getWidth() / tileWidth;
    const auto verticalTileCount = texture->getHeight() / tileHeight;
    m_tileWidth = tileWidth;
    m_tileHeight = tileHeight;
    m_tileCount = horizontalTileCount * verticalTileCount + firstTileIndex;
    m_tiles.resize(static_cast<size_t>(m_tileCount));

    // Reset tile masks
    std::ranges::fill(m_tiles, TileDefinition{});

    for (size_t i = 0; i < masks.size() && i < m_tiles.size(); ++i) {
        m_tiles[i].mask = masks[i];
    }

    // Init source coordinates for each tile
    for (NomadInteger y = 0; y < verticalTileCount; ++y) {
        for (NomadInteger x = 0; x < horizontalTileCount; ++x) {
            const auto tileIndex = y * horizontalTileCount + x + firstTileIndex;
            if (tileIndex >= static_cast<NomadInteger>(m_tiles.size())) {
                log::error("Calculated tile index is outside the tileset");
                return;
            }

            m_tiles[tileIndex].source = Rectangle(
                x * tileWidth,
                y * tileHeight,
                tileWidth,
                tileHeight
            );
        }
    }
}

void Scene::setTileMask(NomadIndex tileIndex, NomadInteger tileMask) {
    if (tileIndex >= m_tiles.size()) {
        return;
    }

    m_tiles[tileIndex].mask = tileMask;
}

NomadInteger Scene::getTileMask(NomadIndex tileIndex) const {
    if (tileIndex >= m_tiles.size()) {
        return 0;
    }

    return m_tiles[tileIndex].mask;
}

void Scene::setTileMapSize(NomadInteger width, NomadInteger height) {
    for (auto& layer : m_layers) {
        std::vector<NomadIndex> newTileLayer(width * height);
        std::vector<NomadIndex> newWallTileLayer(width * height);

        for (NomadInteger y = 0; y < height; ++y) {
            for (NomadInteger x = 0; x < width; ++x) {
                if (x < m_tileMapWidth && y < m_tileMapHeight) {
                    newTileLayer[y * width + x] = layer.tileMap[y * m_tileMapWidth + x];
                } else {
                    newTileLayer[y * width + x] = 0;
                }
            }
        }

        layer.tileMap = std::move(newTileLayer);
    }

    m_tileMapWidth = width;
    m_tileMapHeight = height;
}

NomadInteger Scene::getTileWidth() const {
    return m_tileWidth;
}

NomadInteger Scene::getTileHeight() const {
    return m_tileHeight;
}

NomadInteger Scene::getTileMapWidth() const {
    return m_tileMapWidth;
}

NomadInteger Scene::getTileMapHeight() const {
    return m_tileMapHeight;
}

void Scene::setTileId(const NomadIndex layer, const NomadInteger x, const NomadInteger y, const NomadIndex tileId) {
    if (layer >= m_layers.size()) {
        return;
    }

    auto& tile_layer = m_layers[layer];

    if (x < 0 || x >= m_tileMapWidth || y < 0 || y >= m_tileMapHeight) {
        return;
    }

    tile_layer.tileMap[y * m_tileMapWidth + x] = tileId;
}

NomadIndex Scene::getTileId(const NomadIndex layer, const NomadInteger x, const NomadInteger y) const {
    if (
        layer >= m_layers.size() ||
        x < 0 ||
        x >= m_tileMapWidth ||
        y < 0 ||
        y >= m_tileMapHeight
    ) {
        return 0;
    }

    return m_layers[layer].tileMap[y * m_tileMapWidth + x];
}

NomadInteger Scene::getTileMask(const NomadIndex layerId, const NomadInteger x, const NomadInteger y) const {
    if (layerId >= m_layers.size()) {
        return 0;
    }

    auto tileId = getTileId(layerId, x, y);

    if (tileId >= m_tiles.size()) {
        return 0;
    }

    return m_tiles[tileId].mask;
}

void Scene::processGroundTilesAt(const NomadIndex layer, const Rectangle& rectangle, const TileCallback& callback) const {
    if (layer >= m_layers.size()) {
        return;
    }

    auto& tileLayer = m_layers[layer];

    const auto tileLeft = std::max(rectangle.getLeft() / m_tileWidth, static_cast<NomadInteger>(0));
    const auto tileRight = std::min(rectangle.getRight() / m_tileWidth, m_tileMapWidth - 1);
    const auto tileTop = std::max(rectangle.getTop() / m_tileHeight, static_cast<NomadInteger>(0));
    const auto tileBottom = std::min(rectangle.getBottom() / m_tileHeight, m_tileMapHeight - 1);

    for (NomadInteger y = tileTop; y <= tileBottom; ++y) {
        for (NomadInteger x = tileLeft; x <= tileRight; ++x) {
            const auto tileId = tileLayer.tileMap[y * m_tileMapWidth + x];
            const auto tileMask = m_tiles[tileId].mask;

            TileInformation information {
                x,
                y,
                tileId,
                tileMask
            };

            callback(information);
        }
    }
}

void Scene::setCameraPosition(const PointF &position) {
    m_cameraPosition = position;
}

void Scene::setCameraPosition(NomadFloat x, NomadFloat y) {
    cameraStopFollowEntity();

    setCameraPosition({x, y});
}

void Scene::setCameraX(NomadFloat x) {
    setCameraPosition(x, m_cameraPosition.getY());
}

void Scene::setCameraY(NomadFloat y) {
    setCameraPosition( m_cameraPosition.getX(), y);
}

void Scene::cameraStartFollowEntity(NomadId entity_id) {
    m_cameraFollowEntityId = entity_id;
}

void Scene::cameraStopFollowEntity() {
    m_cameraFollowEntityId = NOMAD_INVALID_ID;
}

PointF Scene::getCameraPosition() const {
    return m_cameraPosition;
}

NomadFloat Scene::getCameraX() const {
    return m_cameraPosition.getX();
}

NomadFloat Scene::getCameraY() const {
    return m_cameraPosition.getY();
}

NomadInteger Scene::getMaskAtLocation(const NomadIndex layer, const PointF &location, const Entity * /*exclude*/) const {
    const auto& l = m_layers[layer];

    const auto x = location.getX();
    const auto y = location.getY();

    const auto tileX = static_cast<NomadInteger>(x) / m_tileWidth;
    const auto tileY = static_cast<NomadInteger>(y) / m_tileHeight;

    NomadInteger mask = 0;

    if (tileX >= 0 && tileX < m_tileMapWidth && tileY >= 0 && tileY < m_tileMapHeight) {
        const auto tileId = l.tileMap[tileY * m_tileMapWidth + tileX];

        if (tileId < m_tiles.size()) {
            mask |= m_tiles[tileId].mask;
        }
    }

    // TODO: Add entities mask at location

    return mask;
}

NomadInteger Scene::getMaskAtEntity(const Entity* entity) const {
    if (entity == nullptr) {
        return 0;
    }

    // return getMaskAtEntity(entity, entity->getLocation());
    const auto layer = entity->getLayer();

    const auto bodyShape = entity->getBodyShape();

    if (bodyShape == BodyShape::Rectangle) {
        const auto width = entity->getBodyWidth();
        const auto height = entity->getBodyHeight();
        const auto x = entity->getX() - width / 2;
        const auto y = entity->getY() - height / 2;

        const RectangleF rectangle(x, y, width, height);

        return getMaskInRectangle(layer, rectangle, entity);
    } else if (bodyShape == BodyShape::Circle) {
        const auto x = entity->getX();
        const auto y = entity->getY();
        const auto radius = entity->getBodyRadius();

        const CircleF circle(x, y, radius);

        return getMaskInCircle(layer, circle, entity);
    } else if (bodyShape == BodyShape::None) {
        // No body shape, so no mask
        return 0;
    } else {
        log::warning("Unexpected body shape: " + std::to_string(static_cast<int>(bodyShape)));
        return 0;
    }
}

NomadInteger Scene::getMaskAtEntity(const Entity* entity, const PointF& location) const {
    // Offset location to sprite.
    const auto x = location.getX() + entity->getX();
    const auto y = location.getY() + entity->getY();

    return getMaskAtLocation(entity->getLayer(), { x, y }, entity);
}

NomadInteger Scene::getMaskFromEntitiesAt(NomadIndex /*layer*/, const RectangleF& /*rectangle*/, const Entity* /*exclude*/) const {
   // if (layer >= m_layers.size()) {
   //     return 0;
   // }
   //
   // NomadInteger mask = 0;
   //
   // for (auto entity : m_layers[layer].entities) {
   //     if (entity == exclude) {
   //         continue;
   //     }
   //
   //     if (entity->is_touching(rectangle)) {
   //         mask |= entity->get_mask();
   //     }
   // }
   //
   // return mask;
    return 0;
}

NomadInteger Scene::getMaskFromEntitiesAt(NomadIndex /*layer*/, const CircleF& /*circle*/, const Entity* /*exclude*/) const {
//    if (layer >= m_layers.size()) {
//        return 0;
//    }
//
//    NomadInteger mask = 0;
//
//    for (auto entity : m_layers[layer].entities) {
//        if (entity == exclude) {
//            continue;
//        }
//
//        if (entity->is_touching(circle)) {
//            mask |= entity->get_mask();
//        }
//    }
//
//    return mask;
    return 0;
}

NomadInteger Scene::getMaskInRectangle(NomadIndex layer, const RectangleF& rectangle, const Entity* exclude) const {
    if (layer >= m_layers.size()) {
        return 0;
    }

    NomadInteger mask = 0;

    mask |= getMaskFromEntitiesAt(layer, rectangle, exclude);

    Rectangle tile_area {
        static_cast<NomadInteger>(rectangle.getLeft()),
        static_cast<NomadInteger>(rectangle.getTop()),
        static_cast<NomadInteger>(rectangle.getRight()),
        static_cast<NomadInteger>(rectangle.getBottom())
    };

    processGroundTilesAt(layer, tile_area, [&mask](const TileInformation& information) {
        mask |= information.mask;
    });

    return mask;
}

NomadInteger Scene::getMaskInCircle(const NomadIndex layer, const CircleF& circle, const Entity* exclude) const {
    if (layer >= m_layers.size()) {
        return 0;
    }

    NomadInteger mask = 0;

    mask |= getMaskFromEntitiesAt(layer, circle, exclude);

    const Rectangle tileArea {
        static_cast<NomadInteger>(circle.getX() - circle.getRadius()),
        static_cast<NomadInteger>(circle.getY() - circle.getRadius()),
        static_cast<NomadInteger>(circle.getX() + circle.getRadius()),
        static_cast<NomadInteger>(circle.getY() + circle.getRadius())
    };

    processGroundTilesAt(layer, tileArea, [&mask](const TileInformation& information) {
        mask |= information.mask;
    });

    return mask;
}

void Scene::registerUserEvent(const NomadString &eventName, const NomadId functionId) {
    m_events.registerUserEvent(eventName, functionId);
}

void Scene::unregisterUserEvent(const NomadString &eventName) {
    m_events.unregisterUserEvent(eventName);
}

void Scene::setOnUpdate(const NomadId functionId) {
    m_events.registerSystemEvent(SystemEvent::Update, functionId);
}

void Scene::clearUpdate() {
    m_events.unregisterSystemEvent(SystemEvent::Update);
}

NomadId Scene::getOnUpdateCallback() const {
    return m_events.getFunctionIdForSystemEvent(SystemEvent::PostUpdate);
}

void Scene::setOnPostUpdateCallback(const NomadId functionId) {
    m_events.registerSystemEvent(SystemEvent::PostUpdate, functionId);
}

void Scene::clearPostUpdateCallback() {
    m_events.unregisterSystemEvent(SystemEvent::PostUpdate);
}

NomadId Scene::getOnPostUpdateCallback() const {
    return m_events.getFunctionIdForSystemEvent(SystemEvent::PostUpdate);
}

void Scene::registerEntityEvent(const NomadString &name, NomadId entityId, NomadId functionId) {
    const auto registrationsIt = std::ranges::find_if(
        m_entityEvents,
        [&name](const auto& event) {
            return event.name == name;
        }
    );

    if (registrationsIt == m_entityEvents.end()) {
        auto registration = std::vector<Event>();
        registration.emplace_back(entityId, functionId);
        m_entityEvents.emplace_back(name, registration);
    } else {
        auto& registrations = registrationsIt->registrations;

        auto it = std::ranges::find_if(
            registrationsIt->registrations,
            [entityId](const Event& registration) {
                return registration.entityId == entityId;
            }
        );

        if (it == registrations.end()) {
            registrations.emplace_back(entityId, functionId);
        } else {
            it->functionId = functionId;
        }
    }
}

void Scene::registerEntityEvent(const NomadId eventId, const NomadId entityId, const NomadId functionId) {
    if (const auto eventDeclaration = m_game->getRuntime()->getEventDefinition(eventId)) {
        registerEntityEvent(eventDeclaration->name, entityId, functionId);
    } else {
        log::warning("Event ID " + toString(eventId) + " not found");
    }
}

void Scene::unregisterEntityEvent(const NomadString &name, NomadId entityId) {
    const auto events_it = std::ranges::find_if(
        m_entityEvents,
        [&name](const auto& event) {
            return event.name == name;
        }
    );

    if (events_it != m_entityEvents.end()) {
        auto& registrations = events_it->registrations;

        std::erase_if(
            registrations,
            [entityId](const Event& registration) {
                return registration.entityId == entityId;
            }
        );
    } else {
        log::warning("Entity ID " + toString(entityId) + " requested to unregister event '" + name + "' but no such event was found");
    }
}

void Scene::unregisterEntityFromAllEvents(NomadId entityId) {
    for (auto&[name, registrations] : m_entityEvents) {
        std::erase_if(
            registrations,
            [entityId](const Event& registration) {
                return registration.entityId == entityId;
            }
        );
    }
}

void Scene::triggerUserEvent(const NomadString &name, Entity *entity) const {
    entity->triggerUserEvent(name);
}

void Scene::dispatchEvent(const EventDispatch& dispatch) {
    if (dispatch.eventId == NOMAD_INVALID_ID) {
        return;
    }

    if (dispatch.sceneId == m_id || dispatch.sceneId == NOMAD_INVALID_ID) {
        for (const auto& layer : m_layers) {
            if (dispatch.layerId == layer.number || dispatch.layerId == NOMAD_INVALID_ID)     {
                for (const auto& entity : layer.entities) {
                    if (dispatch.entityId == entity->getId() || dispatch.entityId == NOMAD_INVALID_ID) {
                        entity->dispatchEvent(dispatch);
                    }
                }
            }
        }
    }
}

void Scene::triggerUserEvent(const NomadString &name) {
    const auto functionId = m_events.getFunctionIdForUserEvent(name);

    if (functionId != NOMAD_INVALID_ID) {
        m_game->executeFunction(functionId, this, nullptr);
    }

    for (const auto& entity: m_entities) {
        entity->triggerUserEvent(name);
    }
    // m_game->triggerSceneEvent(m_id, name);
}

void Scene::triggerEventLayer(const NomadString &name, const NomadIndex layerId) {
    const auto eventsIt = std::ranges::find_if(m_entityEvents,
         [&name](const auto& event) {
             return event.name == name;
        }
    );

    if (eventsIt != m_entityEvents.end()) {
        const auto& registrations = eventsIt->registrations;

        for (const auto&[entityId, functionId] : registrations) {
            const auto entity = getEntityById(entityId);

            if (entity && entity->getLayer() == static_cast<NomadInteger>(layerId)) {
                m_game->executeFunction(functionId, this, entity);
            }
        }
    } else {
        log::warning("Event '" + name + "' not found");
    }
}

void Scene::scheduleEvent(const NomadString &name, NomadInteger frameCount) {
    const auto currentFrame = getFrameNumber();

    m_scheduledEvents.emplace_back(ScheduledEventRegistration{
        name,
        currentFrame + frameCount,
    });

    std::ranges::sort(
        m_scheduledEvents,
        [](const ScheduledEventRegistration& a, const ScheduledEventRegistration& b) {
            return a.frameNumber < b.frameNumber;
        }
    );
}

void Scene::dispatchEvent(const NomadString &name) {
    const auto eventsIt = std::ranges::find_if(
        m_entityEvents,
        [&name](const auto& event) {
            return event.name == name;
        }
     );

    if (eventsIt != m_entityEvents.end()) {
        const auto& registrations = eventsIt->registrations;

        for (const auto& registration : registrations) {
            const auto entity = getEntityById(registration.entityId);

            if (entity) {
                m_game->executeFunction(registration.functionId, this, entity);
            }
        }
    } else {
        // log::warning("No entities registered for event '" + name + "'");
    }
}

void Scene::dispatchEventForEntity(const NomadString &name, Entity *entity) {
    const auto eventsIt = std::ranges::find_if(
         m_entityEvents,
         [&name](const auto& event) {
             return event.name == name;
         }
     );

    if (eventsIt == m_entityEvents.end()) {
        log::warning("Event '" + name + "' not found");
        return;
    }

    const auto& registrations = eventsIt->registrations;

    for (const auto& registration : registrations) {
        if (registration.entityId == entity->getId()) {
            m_game->executeFunction(registration.functionId, this, entity);
            return;
        }
    }
}

void Scene::forEachEntities(const std::function<void(Entity*)>& callback) const {
    for (const auto& entity : m_entities) {
        callback(entity.get());
    }
}

void Scene::forEachEntityByLayer(NomadIndex layerIndex, const std::function<void(Entity*)>& callback) const {
    if (layerIndex >= m_layers.size()) {
        return;
    }

    auto& layer = m_layers[layerIndex];

    for (auto entity : layer.entities) {
        callback(entity);
    }
}

void Scene::removeEntityFromCollisionTracking(Entity* entity) {
    // Remove all collision pairs involving this entity from all layers
    for (auto& layer : m_layers) {
        std::erase_if(
            layer.activeSensorCollisions,
            [entity](const SensorCollisionPair& pair) {
                return pair.sensorEntity == entity || pair.visitorEntity == entity;
            }
        );
    }
}

void Scene::setTint(const Color& color) {
    m_tint = color;
}

const Color& Scene::getTint() const {
    return m_tint;
}

} // nomad
