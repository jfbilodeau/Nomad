// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/Event.hpp>
#include <nomad/script/Closure.hpp>

#include <memory>
#include <variant>
#include <vector>

namespace nomad {

// Forward declarations
class Game;
class RuntimeValue;

struct GameEventCreateScene {
    NomadId sceneId;
    NomadString sceneName;
    NomadId initFunctionId;
    std::unique_ptr<Closure> postCreateClosure;
};

struct GameEventRemoveScene {
    NomadId sceneId;
};

struct GameEventCreateEntity {
    NomadId entityId;
    NomadId sceneId;
    NomadId initFunctionId;
    NomadFloat x, y;
    NomadFloat width, height;
    NomadInteger layer;
    NomadString text;
    std::unique_ptr<Closure> postCreateClosure;
};

struct GameEventRemoveEntity {
    NomadId entityId;
    NomadId sceneId;
};

struct GameEventDispatch {
    EventDispatch dispatch;
};

struct GameEventTriggerEvent {
    NomadString eventName;
};

struct GameEventTriggerSceneEvent {
    NomadId sceneId;
    NomadString eventName;
};

struct GameEventTriggerSceneLayerEvent {
    NomadId sceneId;
    NomadInteger layer;
    NomadString eventName;
};

struct GameEventTriggerEntityEvent {
    NomadId sceneId;
    NomadId entityId;
    NomadInteger triggerAtFrame;
    NomadBoolean repeat;
    NomadString eventName;
    std::vector<RuntimeValue> arguments;
};

struct GameEventTriggerEntityCallback {
    NomadId  sceneId;
    NomadId entityId;
    NomadInteger frameCount;
    NomadInteger triggerAtFrame;
    NomadBoolean repeat;
    NomadId functionId;
};

using GameEvent = std::variant<
    GameEventCreateScene,
    GameEventRemoveScene,
    GameEventCreateEntity,
    GameEventRemoveEntity,
    GameEventDispatch,
    GameEventTriggerEvent,
    GameEventTriggerSceneEvent,
    GameEventTriggerSceneLayerEvent,
    GameEventTriggerEntityEvent,
    GameEventTriggerEntityCallback
>;

}
