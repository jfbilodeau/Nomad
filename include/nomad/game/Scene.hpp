// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/game/ActionManager.hpp>
#include <nomad/game/Entity.hpp>
#include <nomad/game/EventRegistration.hpp>

#include <nomad/geometry/Rectangle.hpp>

#include <box2d/box2d.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace nomad {

constexpr NomadInteger SCENE_LAYER_COUNT = 5;

// Forward declarations
class Canvas;
struct EventDispatch;
class Game;

struct TileInformation {
    NomadInteger x;
    NomadInteger y;
    NomadIndex id;
    NomadInteger mask;
};

using TileCallback = std::function<void(const TileInformation&)>;

struct AnimatedTileDefinition {
    NomadIndex startId;
    NomadIndex count;
    NomadInteger speed;
    NomadInteger startFrame;

    [[nodiscard]] NomadIndex resolveTileId(NomadIndex tileId, NomadInteger frameNumber) const;
};

class Scene {
public:
    explicit Scene(Game* game, NomadId sceneId);
    // Creates a scene with explicit variables for tests and tools that do not initialize Game.
    Scene(NomadId sceneId, const VariableMap* variableMap);
    Scene(const Scene& other) = delete;
    ~Scene();

    [[nodiscard]] Game* getGame() const;

    [[nodiscard]] NomadId getId() const;

    void setName(const NomadString& name);
    [[nodiscard]] NomadString getName() const;

    void setZ(NomadInteger z);
    [[nodiscard]] NomadInteger getZ() const;

    void setFrameNumber(NomadInteger frameNumber);
    [[nodiscard]] NomadInteger getFrameNumber() const;

    void update(Game* game);
    void render(Canvas* canvas) const;
    void processInputEvent(const InputEvent& event);

    void setVariableValue(VariableId variableId, const RuntimeValue& value);
    void getVariableValue(VariableId variableId, RuntimeValue& value) const;

    void addEntity(std::unique_ptr<Entity> entity);
    void removeEntity(Entity* entity);

    [[nodiscard]]
    Entity* getEntityById(NomadId id) const;

    [[nodiscard]]
    NomadId generateEntityId();

    [[nodiscard]]
    Entity* getEntityByName(const NomadString& name) const;
    void getEntitiesByName(const NomadString& name, EntityList& entities) const;

    void pauseOtherEntities(Entity* entity);
    void pauseOtherEntities(const std::vector<Entity*>& entities);
    void pauseAllEntities();
    void unpauseAllEntities();
    void unpauseAllVisibleEntities();

    void loadActionMapping(const NomadString& mappingName);
    void saveActionMapping(const NomadString& mappingName);
    void resetActionMapping(const NomadString& mappingName);

    void addActionPressed(const NomadString& actionName, CallableId callableId, NomadId entityId = NOMAD_INVALID_ID);
    void addActionReleased(const NomadString& actionName, CallableId callableId, NomadId entityId = NOMAD_INVALID_ID);

    void removeActionPressed(const NomadString& actionName, NomadId entityId = NOMAD_INVALID_ID);
    void removeActionReleased(const NomadString& actionName, NomadId entityId = NOMAD_INVALID_ID);

    // Tile map
    void loadTileMap(const NomadString& fileName, const NomadString& tileSetTextureName);
    [[nodiscard]] bool addAnimatedTile(const NomadString& tileName, NomadInteger count, NomadInteger speed);

    void initTileSet(
        const Texture* texture,
        NomadInteger tileWidth,
        NomadInteger tileHeight,
        NomadInteger firstTileIndex,
        const std::vector<NomadInteger>& masks
    );
    void setTileMask(NomadIndex tileIndex, NomadInteger tileMask);
    [[nodiscard]] NomadInteger getTileMask(NomadIndex tileIndex) const;

    void setTileMapSize(NomadInteger width, NomadInteger height);
    [[nodiscard]] NomadInteger getTileWidth() const;
    [[nodiscard]] NomadInteger getTileHeight() const;
    [[nodiscard]] NomadInteger getTileMapWidth() const;
    [[nodiscard]] NomadInteger getTileMapHeight() const;

    void setTileId(NomadInteger layer, NomadInteger x, NomadInteger y, NomadIndex tileId);
    [[nodiscard]] NomadIndex getTileId(NomadInteger layer, NomadInteger x, NomadInteger y) const;
    [[nodiscard]] NomadInteger getTileMask(NomadInteger layerId, NomadInteger x, NomadInteger y) const;

    void processGroundTilesAt(NomadInteger layer, const Rectangle& rectangle, const TileCallback &callback) const;

    // Camera
    void setCameraPosition(const PointF& position);
    void setCameraPosition(NomadFloat x, NomadFloat y);
    void setCameraX(NomadFloat x);
    void setCameraY(NomadFloat y);
    void cameraStartFollowEntity(NomadId entity_id);
    void cameraStopFollowEntity();

    [[nodiscard]] PointF getCameraPosition() const;
    [[nodiscard]] NomadFloat getCameraX() const;
    [[nodiscard]] NomadFloat getCameraY() const;

    // Mask
    [[nodiscard]] NomadInteger getMaskAtLocation(NomadInteger layer, const PointF& location, const Entity* exclude = nullptr) const;
    [[nodiscard]] NomadInteger getMaskAtEntity(const Entity* entity) const;
    [[nodiscard]] NomadInteger getMaskAtEntity(const Entity* entity, const PointF& location) const;
    [[nodiscard]] NomadInteger getMaskFromEntitiesAt(NomadInteger layer, const RectangleF& rectangle, const Entity* exclude) const;
    [[nodiscard]] NomadInteger getMaskFromEntitiesAt(NomadInteger layer, const CircleF& circle, const Entity* exclude) const;
    [[nodiscard]] NomadInteger getMaskInRectangle(NomadInteger layer, const RectangleF& rectangle, const Entity* exclude) const;
    [[nodiscard]] NomadInteger getMaskInCircle(NomadInteger layer, const CircleF& circle, const Entity* exclude) const;

    // Scene events
    void registerUserEvent(const NomadString& eventName, CallableId callableId);
    void unregisterUserEvent(const NomadString& eventName);
    void triggerUserEvent(const NomadString& name);
    void scheduleEvent(const NomadString &name, NomadInteger frameCount);

    void setOnUpdate(CallableId callableId);
    void clearUpdate();
    [[nodiscard]] CallableId getOnUpdateCallback() const;
    void setOnPostUpdateCallback(CallableId callableId);
    void clearPostUpdateCallback();
    [[nodiscard]] CallableId getOnPostUpdateCallback() const;

    // Layer events
    void triggerEventLayer(const NomadString& name, NomadInteger layer);

    // Entity events
    void registerEntityEvent(const NomadString& name, NomadId entityId, CallableId callableId);
    void registerEntityEvent(EventId eventId, NomadId entityId, CallableId callableId);
    void unregisterEntityEvent(const NomadString& name, NomadId entityId);
    void unregisterEntityFromAllEvents(NomadId entityId);
    void triggerUserEvent(const NomadString& name, Entity* entity) const;

    void dispatchEvent(const EventDispatch& dispatch);
    void dispatchEvent(const NomadString& name);
    void dispatchEventForEntity(const NomadString& name, Entity* entity);

    // Entity iteration
    void forEachEntities(const std::function<void(Entity*)>& callback) const;
    void forEachEntityByLayer(NomadInteger layerIndex, const std::function<void(Entity*)>& callback) const;

    // Presentation / FXs
    void setTint(const Color& color);
    [[nodiscard]] const Color& getTint() const;

private: // structs
    struct TileDefinition {
        NomadInteger mask = 0;
        Rectangle source;
        NomadString name;
    };

    struct SensorCollisionPair {
        b2ShapeId sensorShapeId;
        b2ShapeId visitorShapeId;
        Entity* sensorEntity;
        Entity* visitorEntity;
        NomadInteger visitorMask;
    };

    struct Layer {
        Layer() = default;
        // Discovered a bug where I copied layer instead of referencing them. Let's make sure that doesn't happen again...
        Layer(const Layer& other) = delete;
        Layer& operator=(const Layer& other) = delete;

        NomadInteger number = -1;
        EntityList entities;
        std::vector<NomadIndex> tileMap;
        NomadBoolean hasTileMap = false;
        std::vector<b2BodyId> bodies;
        NomadBoolean bodiesInvalidated = true;

        b2WorldId worldId = {};  // Initialized by Scene
        std::vector<SensorCollisionPair> activeSensorCollisions;
    };

    struct ActionMapping {
        NomadString name;
        InputAction action = InputAction::Unknown;
        CallableId callableId;
        NomadId entityId = NOMAD_INVALID_ID;
        NomadBoolean pressed = false;
        NomadBoolean released = false;
        NomadBoolean held = false;
    };

    struct AddedEntity {
        CallableId initFunctionId;
        NomadFloat x, y;
        NomadInteger layer;
        NomadId id;
        NomadString textId;
    };

    struct Event {
        NomadId entityId;
        CallableId callableId;
    };

    struct EventRegistrations {
        // Event name
        NomadString name;
        std::vector<Event> registrations;
    };

    struct ScheduledEventRegistration {
        NomadString name;
        NomadInteger frameNumber;
    };

private: // methods
    void initializePhysicsLayers();

    ActionMapping* getActionMapping(const NomadString& name, InputAction type, NomadId entityId = NOMAD_INVALID_ID);

    void removeEntityFromCollisionTracking(Entity* entity);

    void updateScheduledEvents();
    void updatePhysics();
    void generateLayerPhysicsBodies(std::array<Scene::Layer, 5>::value_type &layer) const;
    void updateEntityLayers();
    void updateCamera();

    void renderTileMap(const Canvas* canvas, const Layer& layer) const;
    void renderTile(const Canvas* canvas, Coord y, Coord x, NomadIndex tileIndex) const;

private: // data
    Game* m_game;
    NomadId m_id = NOMAD_INVALID_ID;
    NomadString m_name;
    NomadInteger m_z = 0;
    NomadInteger m_frameNumber = 0;

    VariableList m_variables;

    // Entities
    NomadId m_entityIdCounter = NOMAD_ID_MIN;

    std::vector<std::unique_ptr<Entity>> m_entities;

    // Physics
    bool m_physicsEnabled = true;

    // Layers
    std::array<Layer, SCENE_LAYER_COUNT> m_layers;

    // Tile map
    const Texture* m_tileTexture = nullptr;
    NomadInteger m_tileCount = 0;
    NomadInteger m_tileWidth = 0, m_tileHeight = 0;
    NomadInteger m_tileMapWidth = 0, m_tileMapHeight = 0;
    std::vector<TileDefinition> m_tiles;
    std::vector<AnimatedTileDefinition> m_animatedTiles;

    // Camera
    PointF m_cameraPosition;
    NomadId m_cameraFollowEntityId = NOMAD_INVALID_ID;

    // Events
    std::vector<EventRegistrations> m_entityEvents;
    std::vector<ScheduledEventRegistration> m_scheduledEvents;

    EventRegistrationManager m_events;
    ActionManager m_actionManager;
    std::vector<ActionMapping> m_actionMapping;

    // SFX
    Color m_tint = Colors::Transparent;
};

} // nomad
