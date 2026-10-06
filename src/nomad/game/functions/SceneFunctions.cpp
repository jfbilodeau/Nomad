// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/TempHeap.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/resource/ResourceManager.hpp>

#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Closure.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

#define CHECK_SCENE_NOT_NULL(message) \
    auto scene = getCurrentContext()->getScene(); \
    if (scene == nullptr) { \
        log::error(message); \
        return; \
    }

void Game::initSceneFunctions() {
    log::debug("Initializing scene functions");

    m_runtime->registerNativeFunction(
        "scene.addAnimatedTile",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot add an animated tile outside of a scene")

            const auto tileName = interpreter->getStringParameter(0);
            const auto count = interpreter->getIntegerParameter(1);
            const auto speed = interpreter->getIntegerParameter(2);

            if (!scene->addAnimatedTile(tileName, count, speed)) {
                log::error("Cannot add animated tile with an unknown name, invalid count, or invalid speed");
            }
        }, {
            defParameter("tileName", m_runtime->getStringRefType(), NomadParamDoc("Name of the first tile in the animation.")),
            defParameter("count", m_runtime->getIntegerType(), NomadParamDoc("Number of consecutive tiles (frames) to animate.")),
            defParameter("speed", m_runtime->getIntegerType(), NomadParamDoc("Scene updates per displayed tile frame."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Adds a looping animation for a consecutive range of tiles.")
    );

    m_runtime->registerNativeFunction(
        "scene.camera.follow",
        [this](VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot set camera position outside of a scene")

            auto entityId = interpreter->getIdParameter(0);

            scene->cameraStartFollowEntity(entityId);
        }, {
            defParameter("entityId", m_runtime->getIntegerType(), NomadParamDoc("ID of the entity to follow."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Makes the camera follow the specified entity.")
    );

    m_runtime->registerNativeFunction(
        "scene.camera.setPosition",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot set camera position outside of a scene")

            auto x = interpreter->getFloatParameter(0);
            auto y = interpreter->getFloatParameter(1);

            scene->setCameraPosition(x, y);
        }, {
            defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X position of the camera.")),
            defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y position of the camera."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Sets the camera position.")
    );

    m_runtime->registerNativeFunction(
        "scene.createEntity",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot create entity outside of a scene")

            const auto functionId = interpreter->getIdParameter(0);
            const auto entityX = interpreter->getFloatParameter(1);
            const auto entityY = interpreter->getFloatParameter(2);
            const auto layer = interpreter->getIntegerParameter(3);

            addEntityToScene(
                scene->getId(),
                functionId,
                entityX,
                entityY,
                0.0,
                0.0,
                layer,
                ""
            );
        }, {
            defParameter(
                "function",
                m_runtime->getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("Function to initialize the entity.")
            ),
            defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X position of the entity.")),
            defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y position of the entity.")),
            defParameter("layer", m_runtime->getIntegerType(), NomadParamDoc("Layer of the entity."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Creates a new entity for this scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.createEntityByName",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot create entity outside of a scene")

            const auto initFunctionName = interpreter->getStringParameter(0);
            const auto entityX = interpreter->getFloatParameter(1);
            const auto entityY = interpreter->getFloatParameter(2);
            const auto layer = interpreter->getIntegerParameter(3);

            const NomadId initFunctionId = getFunctionId(initFunctionName);

            if (initFunctionId != NOMAD_INVALID_ID) {
                addEntityToScene(
                    scene->getId(),
                    initFunctionId,
                    entityX,
                    entityY,
                    0.0,
                    0.0,
                    layer,
                    ""
                );
            } else {
                log::warning(NomadString("Function '") + initFunctionName + "' not found. Cannot create entity.");
            }
        }, {
            defParameter(
                "functionName", m_runtime->getStringRefType(),
                NomadParamDoc("Name of the function to execute to initialize the entity.")
            ),
            defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X position of the entity.")),
            defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y position of the entity.")),
            defParameter("layer", m_runtime->getIntegerType(), NomadParamDoc("Layer of the entity."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Creates a new entity for this scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.createEntityByNameThen",
        [this](VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot create entity outside of a scene")

            auto initFunctionName = interpreter->getStringParameter(0);
            const auto entityX = interpreter->getFloatParameter(1);
            const auto entityY = interpreter->getFloatParameter(2);
            const auto layer = interpreter->getIntegerParameter(3);
            const auto functionId = interpreter->getIdParameter(4);
            auto postFunctionClosure = interpreter->createClosure(m_runtime.get(), functionId);

            addEntityToScene(
                scene->getId(),
                getFunctionId(initFunctionName),
                entityX,
                entityY,
                0.0,
                0.0,
                layer,
                "",
                std::move(postFunctionClosure)
            );
        }, {
            defParameter(
                "functionName", m_runtime->getStringRefType(),
                NomadParamDoc("Name of the function to execute to initialize the entity.")
            ),
            defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X position of the entity.")),
            defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y position of the entity.")),
            defParameter("layer", m_runtime->getIntegerType(), NomadParamDoc("Layer of the entity.")),
            defParameter("function", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("ID of the function to execute after creating the entity."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Creates a new entity for this scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.createEntityThen",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot create entity outside of a scene")

            auto functionId = interpreter->getIdParameter(0);
            const auto entityX = interpreter->getFloatParameter(1);
            const auto entityY = interpreter->getFloatParameter(2);
            const auto layer = interpreter->getIntegerParameter(3);
            const auto postFunctionId = interpreter->getIdParameter(4);
            auto postFunctionClosure = interpreter->createClosure(m_runtime.get(), postFunctionId);

            addEntityToScene(
                scene->getId(),
                functionId,
                entityX,
                entityY,
                0.0,
                0.0,
                layer,
                "",
                std::move(postFunctionClosure)
            );
        }, {
            defParameter(
                "function",
                m_runtime->getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("Name of the function to execute to initialize the entity.")
            ),
            defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X position of the entity.")),
            defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y position of the entity.")),
            defParameter("layer", m_runtime->getIntegerType(), NomadParamDoc("Layer of the entity.")),
            defParameter("function", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("ID of the function to execute after creating the entity."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Creates a new entity for this scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.createTextByNameEntity",
        [this](VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot create entity outside of a scene")

            auto initFunctionName = interpreter->getStringParameter(0);
            const auto entityX = interpreter->getFloatParameter(1);
            const auto entityY = interpreter->getFloatParameter(2);
            const auto layer = interpreter->getIntegerParameter(3);
            const auto text = interpreter->getStringParameter(4);

            addEntityToScene(
                scene->getId(),
                getFunctionId(initFunctionName),
                entityX,
                entityY,
                0.0,
                0.0,
                layer,
                text
            );
        }, {
            defParameter(
                "functionName", m_runtime->getStringRefType(),
                NomadParamDoc("Name of the function to execute to initialize the entity.")
            ),
            defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X position of the entity.")),
            defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y position of the entity.")),
            defParameter("layer", m_runtime->getIntegerType(), NomadParamDoc("Layer of the entity.")),
            defParameter("text", m_runtime->getStringRefType(), NomadParamDoc("Text to display for the entity."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Creates a new entity for this scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.createTextEntity",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot create entity outside of a scene")

            const auto function = interpreter->getIdParameter(0);
            const auto entityX = interpreter->getFloatParameter(1);
            const auto entityY = interpreter->getFloatParameter(2);
            const auto layer = interpreter->getIntegerParameter(3);
            const auto text = interpreter->getStringParameter(4);

            addEntityToScene(
                scene->getId(),
                function,
                entityX,
                entityY,
                0.0,
                0.0,
                layer,
                text
            );
        }, {
            defParameter(
                "function",
                m_runtime->getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("Name of the function to execute to initialize the entity.")
            ),
            defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X position of the entity.")),
            defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y position of the entity.")),
            defParameter("layer", m_runtime->getIntegerType(), NomadParamDoc("Layer of the entity.")),
            defParameter("text", m_runtime->getStringRefType(), NomadParamDoc("Text to display for the entity."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Creates a new entity for this scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.createTextEntityAndThen",
        [this](VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot create entity outside of a scene")

            auto initFunctionName = interpreter->getStringParameter(0);
            const auto entityX = interpreter->getFloatParameter(1);
            const auto entityY = interpreter->getFloatParameter(2);
            const auto layer = interpreter->getIntegerParameter(3);
            const auto text = interpreter->getStringParameter(4);
            const auto functionId = interpreter->getIdParameter(5);
            auto postFunctionClosure = interpreter->createClosure(m_runtime.get(), functionId);

            addEntityToScene(
                scene->getId(),
                getFunctionId(initFunctionName),
                entityX,
                entityY,
                0.0,
                0.0,
                layer,
                text,
                std::move(postFunctionClosure)
            );
        }, {
            defParameter(
                "functionName", m_runtime->getStringRefType(),
                NomadParamDoc("Name of the function to execute to initialize the entity.")
            ),
            defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X position of the entity.")),
            defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y position of the entity.")),
            defParameter("layer", m_runtime->getIntegerType(), NomadParamDoc("Layer of the entity.")),
            defParameter("text", m_runtime->getStringRefType(), NomadParamDoc("Text to display for the entity.")),
            defParameter("function", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("ID of the function to execute after creating the entity."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Creates a new entity for this scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.loadInputMapping",
        [this](VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot load input mapping outside of a scene")

            auto mappingName = interpreter->getStringParameter(0);

            scene->loadActionMapping(mappingName);
        }, {
            defParameter(
                "mappingName", m_runtime->getStringRefType(), NomadParamDoc("Name of the input mapping to load.")
            )
        },
        m_runtime->getVoidType(),
        NomadDoc("Load an input mapping for this scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.loadMap",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot load input mapping outside of a scene")

            const NomadString mapName = interpreter->getStringParameter(0);
            const NomadString tileSetTexture = interpreter->getStringParameter(1);

            const auto mapFileName = mapName + ".tmj";
            const auto tileSetTextureFileName = tileSetTexture + ".png";

            scene->loadTileMap(mapFileName, tileSetTextureFileName);
        }, {
            defParameter("mapName", m_runtime->getStringRefType(), NomadParamDoc("Name of the map to load.")),
            defParameter("tileSetTexture", m_runtime->getStringRefType(), NomadParamDoc("Name of the tile set texture to load."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Loads a map for this scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.on",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot set onEvent outside of a scene")

            const auto eventId = interpreter->getParameter(0).getIdValue();
            const auto functionId = interpreter->getParameter(1).getIdValue();

            const auto event = m_runtime->getEventDefinition(eventId);

            if (!event) {
                log::error(NomadString("Invalid event id for scene.on: ") + toString(eventId));
                return;
            }

            scene->registerUserEvent(event->name, functionId);
        },
        {
            defParameter(
                "event",
                m_runtime->getEventCallbackType(),
                NomadParamDoc("The event to set the callback for")
            )
        },
        m_runtime->getVoidType(),
        NomadDoc("Sets the onEvent callback for this scene. The callback function is called when the specified event is triggered.")
    );

    m_runtime->registerNativeFunction(
        "scene.onPostUpdate",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot set onPostUpdate outside of a scene")

            const auto functionId = interpreter->getIdParameter(0);

            scene->setOnPostUpdateCallback(functionId);
        },
        {
            defParameter(
                "function",
                m_runtime->getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("Function to execute after each update of the scene")
            )
        },
        m_runtime->getVoidType(),
        NomadDoc("Sets the onPostUpdate callback for this scene. The callback function is called after updating the entities in the scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.onUpdate",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot set onUpdate outside of a scene")

            const auto functionId = interpreter->getIdParameter(0);

            scene->setOnUpdate(functionId);
        },
        {
            defParameter(
                "function",
                m_runtime->getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("Function to execute on each update of the scene")
            )
        },
        m_runtime->getVoidType(),
        NomadDoc("Sets the onUpdate callback for this scene. The callback function is called before updating the entities in the scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.pauseAll",
        [this](VirtualMachine* /*interpreter*/) {
            CHECK_SCENE_NOT_NULL("Cannot pause entities outside of a scene")

            scene->pauseAllEntities();
        },
        {},
        m_runtime->getVoidType(),
        NomadDoc("Pauses all entities in this scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.queue",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot trigger event outside of a scene")

            auto eventName = interpreter->getStringParameter(1);
            auto frameCount = interpreter->getIntegerParameter(0);

            scene->scheduleEvent(eventName, frameCount);
        },
        {
            defParameter("frameCount", m_runtime->getIntegerType(), NomadParamDoc("Number of frames to wait before triggering the event.")),
            defParameter("eventName", m_runtime->getStringRefType(), NomadParamDoc("Name of the event to trigger."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Trigger an event after a specified number of frames.")
    );

    m_runtime->registerNativeFunction(
        "scene.removeSelf",
        [this](VirtualMachine* /*interpreter*/) {
            CHECK_SCENE_NOT_NULL("Cannot call `scene.removeSelf` outside of a scene")

            removeScene(scene);
        },
        {},
        m_runtime->getVoidType(),
        NomadDoc("Unloads this scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.trigger",
        [this](VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot trigger event outside of a scene")

            const auto eventId = interpreter->getIdParameter(0);

            std::vector<RuntimeValue> args;

            const auto event = m_runtime->getEventDefinition(eventId);

            if (!event) {
                log::error(NomadString("Invalid event id for scene.trigger: ") + toString(eventId));
                return;
            }

            for (std::size_t i = 0; i < event->parameters.size(); ++i) {
                const auto& parameter = event->parameters[i];

                const auto type = m_runtime->getType(parameter.typeId);

                RuntimeValue value;

                if (type) {
                    (*type)->copyValue(interpreter->peekStack(static_cast<NomadIndex>(i) + 1), value);
                } else {
                    log::error("Invalid type id for event parameter: " + toString(parameter.typeId));
                }

                args.emplace_back(value);
            }

            scene->getGame()->dispatchEvent(EventDispatch {
                eventId,
                scene->getId(),
                NOMAD_INVALID_ID,
                NOMAD_INVALID_ID,
                0,
                std::move(args),
            });
        }, {
            defParameter("event", m_runtime->getEventDispatchType(), NomadParamDoc("The event to dispatch (trigger)."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Trigger an event for all entities on this scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.trigger.layer",
        [this](VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot trigger event outside of a scene")

            const auto eventId = interpreter->getIdParameter(0);
            const auto layerId = interpreter->getIntegerParameter(1);

            std::vector<RuntimeValue> args;

            const auto event = m_runtime->getEventDefinition(eventId);

            if (!event) {
                log::error(NomadString("Invalid event id for scene.trigger.layer: ") + toString(eventId));
                return;
            }

            for (std::size_t i = 0; i < event->parameters.size(); ++i) {
                const auto& parameter = event->parameters[i];

                const auto type = m_runtime->getType(parameter.typeId);

                RuntimeValue value;

                if (type) {
                    (*type)->copyValue(interpreter->peekStack(static_cast<NomadIndex>(i) + 2), value);
                } else {
                    log::error("Invalid type id for event parameter: " + toString(parameter.typeId));
                }

                args.emplace_back(value);
            }

            scene->getGame()->dispatchEvent(EventDispatch {
                eventId,
                scene->getId(),
                static_cast<NomadId>(layerId),
                NOMAD_INVALID_ID,
                0,
                std::move(args),
            });
        }, {
            defParameter("event", m_runtime->getEventDispatchType(), NomadParamDoc("The event to dispatch (trigger).")),
            defParameter("layerId", m_runtime->getIntegerType(), NomadParamDoc("Layer ID to trigger the event on."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Trigger an event for all entities on the specified layer.")
    );

    m_runtime->registerNativeFunction(
        "scene.unpauseAll",
        [this](VirtualMachine* /*interpreter*/) {
            CHECK_SCENE_NOT_NULL("Cannot unpause entities outside of a scene")

            scene->unpauseAllEntities();
        },
        {},
        m_runtime->getVoidType(),
        NomadDoc("Unpauses all entities in this scene.")
    );

    m_runtime->registerNativeFunction(
        "select",
        [this](VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot load input mapping outside of a scene")

            auto predicateId = interpreter->getIdParameter(0);

            auto executionContext = getCurrentContext();
            auto thisEntity = executionContext->getThisEntity();

            auto otherEntities = createTempVector<Entity*>();

            NomadInteger layerIndex = thisEntity->getLayer();

            scene->forEachEntityByLayer(
                layerIndex,
                [&](Entity* entity) {
                    if (entity != thisEntity) {
                        executionContext->clearOtherEntitiesAndAdd(entity);

                        const auto result = executePredicate(predicateId);

                        if (result) {
                            otherEntities.push_back(entity);
                        }
                    }
                }
            );

            executionContext->setOtherEntities(otherEntities);
        }, {
            defParameter(
                "predicate", m_runtime->getPredicateType(),
                NomadParamDoc("The predicate used to select other entities")
            ),
        },
        m_runtime->getIntegerType(),
        NomadDoc("Select entities in the same layer as the `this` entity that match the predicate.")
    );

    m_runtime->registerNativeFunction(
        "select.all",
        [this](VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot load input mapping outside of a scene")

            auto predicateId = interpreter->getIdParameter(0);

            auto executionContext = getCurrentContext();
            auto thisEntity = executionContext->getThisEntity();

            auto otherEntities = createTempVector<Entity*>();

            executionContext->clearOtherEntities();

            scene->forEachEntities(
                [&](Entity* entity) {
                    if (entity != thisEntity) {
                        executionContext->clearOtherEntitiesAndAdd(entity);

                        auto result = executePredicate(predicateId);

                        if (result) {
                            otherEntities.push_back(entity);
                        }
                    }
                }
            );

            executionContext->setOtherEntities(otherEntities);
        }, {
            defParameter(
                "predicate", m_runtime->getPredicateType(),
                NomadParamDoc("The predicate used to select other entities")
            ),
        },
        m_runtime->getIntegerType(),
        NomadDoc("Select entities in all layers that match the predicate.")
    );

    m_runtime->registerNativeFunction(
        "select.byName",
        [this](VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot load input mapping outside of a scene")

            auto name = interpreter->getStringParameter(0);

            auto executionContext = getCurrentContext();

            executionContext->clearOtherEntities();

            scene->forEachEntities(
                [&](Entity* entity) {
                    if (entity->getName() == name) {
                        executionContext->addOtherEntity(entity);
                    }
                }
            );
        }, {
            defParameter("name", m_runtime->getStringRefType(), NomadParamDoc("The name of the entity to select")),
        },
        m_runtime->getIntegerType(),
        NomadDoc("Select entities across all layers that have the given name.")
    );

    m_runtime->registerNativeFunction(
        "select.this",
        [this](VirtualMachine* /*interpreter*/) {
            CHECK_SCENE_NOT_NULL("Cannot load input mapping outside of a scene")

            auto executionContext = getCurrentContext();
            auto thisEntity = executionContext->getThisEntity();

            executionContext->clearOtherEntities();
            executionContext->addOtherEntity(thisEntity);
        },
        { },
        m_runtime->getVoidType(),
        NomadDoc("Select the `this` as `other` entity. Useful to pass the `this` entity to a function that takes `other` entities.")
    );


}

} // nomad
