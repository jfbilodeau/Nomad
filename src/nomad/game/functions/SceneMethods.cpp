// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Scene.hpp>
#include <nomad/game/SceneObject.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <utility>

namespace nomad {

void Game::initSceneMethods() {
    const auto typeId = m_runtime->getTypeId("game.Scene");
    const auto voidTypeId = m_runtime->getVoidType()->getId();
    const auto integerTypeId = m_runtime->getIntegerType()->getId();
    const auto floatTypeId = m_runtime->getFloatType()->getId();
    const auto stringTypeId = m_runtime->getStringType()->getId();
    const auto callbackTypeId = m_runtime->getTypeManager().getCallbackType(
        {}, m_runtime->getVoidType()
    )->getId();

    const auto animatedTileId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "addAnimatedTile",
        .parameters = {
            {stringTypeId, "tileName", NomadParamDoc("Name of the first tile in the animation.")},
            {integerTypeId, "count", NomadParamDoc("Number of consecutive tiles (frames) to animate.")},
            {integerTypeId, "speed", NomadParamDoc("Scene updates per displayed tile frame.")}
        },
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& scene = machine->getThisAs<SceneObject>()->getScene();
            if (!scene.addAnimatedTile(
                machine->getStringParameter(0)->getString(),
                machine->getIntegerParameter(1),
                machine->getIntegerParameter(2)
            )) {
                log::error("Cannot add animated tile with an unknown name, invalid count, or invalid speed");
            }
        },
        .documentation = NomadDoc("Adds a looping animation for a consecutive range of tiles.")
    });

    const auto createEntityId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "createEntity",
        .parameters = {
            {callbackTypeId, "function", NomadParamDoc("Function to initialize the entity.")},
            {floatTypeId, "x", NomadParamDoc("X position of the entity.")},
            {floatTypeId, "y", NomadParamDoc("Y position of the entity.")},
            {integerTypeId, "layer", NomadParamDoc("Layer of the entity.")}
        },
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& scene = machine->getThisAs<SceneObject>()->getScene();
            scene.getGame()->addEntityToScene(
                scene.getId(),
                machine->getCallableIdParameter(0),
                machine->getFloatParameter(1),
                machine->getFloatParameter(2),
                0.0,
                0.0,
                machine->getIntegerParameter(3),
                ""
            );
        },
        .documentation = NomadDoc("Creates a new entity for this scene.")
    });

    const auto createEntityByNameId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "createEntityByName",
        .parameters = {
            {stringTypeId, "functionName", NomadParamDoc("Name of the function to initialize the entity.")},
            {floatTypeId, "x", NomadParamDoc("X position of the entity.")},
            {floatTypeId, "y", NomadParamDoc("Y position of the entity.")},
            {integerTypeId, "layer", NomadParamDoc("Layer of the entity.")}
        },
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& scene = machine->getThisAs<SceneObject>()->getScene();
            auto* game = scene.getGame();
            const auto& functionName = machine->getStringParameter(0)->getString();
            const auto functionId = game->getFunctionId(functionName);
            if (functionId.isInvalid()) {
                log::warning("Function '" + functionName + "' not found. Cannot create entity.");

                return;
            }

            game->addEntityToScene(
                scene.getId(),
                functionId,
                machine->getFloatParameter(1),
                machine->getFloatParameter(2),
                0.0,
                0.0,
                machine->getIntegerParameter(3),
                ""
            );
        },
        .documentation = NomadDoc("Creates a new entity for this scene.")
    });

    const auto createEntityByNameThenId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "createEntityByNameThen",
        .parameters = {
            {stringTypeId, "functionName", NomadParamDoc("Name of the function to initialize the entity.")},
            {floatTypeId, "x", NomadParamDoc("X position of the entity.")},
            {floatTypeId, "y", NomadParamDoc("Y position of the entity.")},
            {integerTypeId, "layer", NomadParamDoc("Layer of the entity.")},
            {callbackTypeId, "function", NomadParamDoc("Function to execute after creating the entity.")}
        },
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& scene = machine->getThisAs<SceneObject>()->getScene();
            auto* game = scene.getGame();
            const auto& functionName = machine->getStringParameter(0)->getString();
            const auto callbackId = machine->getCallableIdParameter(4);
            auto callback = machine->createClosure(game->getRuntime(), callbackId);
            game->addEntityToScene(
                scene.getId(),
                game->getFunctionId(functionName),
                machine->getFloatParameter(1),
                machine->getFloatParameter(2),
                0.0,
                0.0,
                machine->getIntegerParameter(3),
                "",
                std::move(callback)
            );
        },
        .documentation = NomadDoc("Creates an entity and executes a callback after creation.")
    });

    const auto createEntityThenId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "createEntityThen",
        .parameters = {
            {callbackTypeId, "function", NomadParamDoc("Function to initialize the entity.")},
            {floatTypeId, "x", NomadParamDoc("X position of the entity.")},
            {floatTypeId, "y", NomadParamDoc("Y position of the entity.")},
            {integerTypeId, "layer", NomadParamDoc("Layer of the entity.")},
            {callbackTypeId, "function", NomadParamDoc("Function to execute after creating the entity.")}
        },
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& scene = machine->getThisAs<SceneObject>()->getScene();
            auto* game = scene.getGame();
            const auto functionId = machine->getCallableIdParameter(0);
            const auto callbackId = machine->getCallableIdParameter(4);
            auto callback = machine->createClosure(game->getRuntime(), callbackId);
            game->addEntityToScene(
                scene.getId(),
                functionId,
                machine->getFloatParameter(1),
                machine->getFloatParameter(2),
                0.0,
                0.0,
                machine->getIntegerParameter(3),
                "",
                std::move(callback)
            );
        },
        .documentation = NomadDoc("Creates an entity and executes a callback after creation.")
    });

    const auto createTextByNameEntityId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "createTextByNameEntity",
        .parameters = {
            {stringTypeId, "functionName", NomadParamDoc("Name of the function to initialize the entity.")},
            {floatTypeId, "x", NomadParamDoc("X position of the entity.")},
            {floatTypeId, "y", NomadParamDoc("Y position of the entity.")},
            {integerTypeId, "layer", NomadParamDoc("Layer of the entity.")},
            {stringTypeId, "text", NomadParamDoc("Text to display for the entity.")}
        },
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& scene = machine->getThisAs<SceneObject>()->getScene();
            auto* game = scene.getGame();
            game->addEntityToScene(
                scene.getId(),
                game->getFunctionId(machine->getStringParameter(0)->getString()),
                machine->getFloatParameter(1),
                machine->getFloatParameter(2),
                0.0,
                0.0,
                machine->getIntegerParameter(3),
                machine->getStringParameter(4)->getString()
            );
        },
        .documentation = NomadDoc("Creates a text entity for this scene.")
    });

    const auto createTextEntityId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "createTextEntity",
        .parameters = {
            {callbackTypeId, "function", NomadParamDoc("Function to initialize the entity.")},
            {floatTypeId, "x", NomadParamDoc("X position of the entity.")},
            {floatTypeId, "y", NomadParamDoc("Y position of the entity.")},
            {integerTypeId, "layer", NomadParamDoc("Layer of the entity.")},
            {stringTypeId, "text", NomadParamDoc("Text to display for the entity.")}
        },
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& scene = machine->getThisAs<SceneObject>()->getScene();
            scene.getGame()->addEntityToScene(
                scene.getId(),
                machine->getCallableIdParameter(0),
                machine->getFloatParameter(1),
                machine->getFloatParameter(2),
                0.0,
                0.0,
                machine->getIntegerParameter(3),
                machine->getStringParameter(4)->getString()
            );
        },
        .documentation = NomadDoc("Creates a text entity for this scene.")
    });

    const auto createTextEntityAndThenId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "createTextEntityAndThen",
        .parameters = {
            {stringTypeId, "functionName", NomadParamDoc("Name of the function to initialize the entity.")},
            {floatTypeId, "x", NomadParamDoc("X position of the entity.")},
            {floatTypeId, "y", NomadParamDoc("Y position of the entity.")},
            {integerTypeId, "layer", NomadParamDoc("Layer of the entity.")},
            {stringTypeId, "text", NomadParamDoc("Text to display for the entity.")},
            {callbackTypeId, "function", NomadParamDoc("Function to execute after creating the entity.")}
        },
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& scene = machine->getThisAs<SceneObject>()->getScene();
            auto* game = scene.getGame();
            const auto callbackId = machine->getCallableIdParameter(5);
            auto callback = machine->createClosure(game->getRuntime(), callbackId);
            game->addEntityToScene(
                scene.getId(),
                game->getFunctionId(machine->getStringParameter(0)->getString()),
                machine->getFloatParameter(1),
                machine->getFloatParameter(2),
                0.0,
                0.0,
                machine->getIntegerParameter(3),
                machine->getStringParameter(4)->getString(),
                std::move(callback)
            );
        },
        .documentation = NomadDoc("Creates a text entity and executes a callback after creation.")
    });

    const auto inputMappingId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "loadInputMapping",
        .parameters = {{stringTypeId, "mappingName", NomadParamDoc("Name of the input mapping to load.")}},
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& scene = machine->getThisAs<SceneObject>()->getScene();
            scene.loadActionMapping(machine->getStringParameter(0)->getString());
        },
        .documentation = NomadDoc("Loads an input mapping for this scene.")
    });

    const auto mapId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "loadMap",
        .parameters = {
            {stringTypeId, "mapName", NomadParamDoc("Name of the map to load.")},
            {stringTypeId, "tileSetTexture", NomadParamDoc("Name of the tile set texture to load.")}
        },
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& scene = machine->getThisAs<SceneObject>()->getScene();
            scene.loadTileMap(
                machine->getStringParameter(0)->getString() + ".tmj",
                machine->getStringParameter(1)->getString() + ".png"
            );
        },
        .documentation = NomadDoc("Loads a map for this scene.")
    });

    const auto pauseId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "pauseAll",
        .parameters = {},
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            machine->getThisAs<SceneObject>()->getScene().pauseAllEntities();
        },
        .documentation = NomadDoc("Pauses all entities in this scene.")
    });

    const auto removeId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "removeSelf",
        .parameters = {},
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& scene = machine->getThisAs<SceneObject>()->getScene();
            scene.getGame()->removeScene(&scene);
        },
        .documentation = NomadDoc("Queues removal of this scene.")
    });

    const auto unpauseId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "unpauseAll",
        .parameters = {},
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            machine->getThisAs<SceneObject>()->getScene().unpauseAllEntities();
        },
        .documentation = NomadDoc("Unpauses all entities in this scene.")
    });

    if (
        animatedTileId.isInvalid() ||
        createEntityId.isInvalid() ||
        createEntityByNameId.isInvalid() ||
        createEntityByNameThenId.isInvalid() ||
        createEntityThenId.isInvalid() ||
        createTextByNameEntityId.isInvalid() ||
        createTextEntityId.isInvalid() ||
        createTextEntityAndThenId.isInvalid() ||
        inputMappingId.isInvalid() ||
        mapId.isInvalid() ||
        pauseId.isInvalid() ||
        removeId.isInvalid() ||
        unpauseId.isInvalid()
    ) {
        raiseError("Failed to register scene methods");
    }
}

} // namespace nomad
