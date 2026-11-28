// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/game/resource/ResourceManager.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/TempHeap.hpp>

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
        "scene.on",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot set onEvent outside of a scene")

            const auto eventId = EventId{interpreter->getIdParameter(0)};
            const auto functionId = interpreter->getCallableIdParameter(1);

            const auto* event = m_runtime->getEventDefinition(eventId);

            if (!event) {
                log::error(NomadString("Invalid event id for scene.on: ") + toString(eventId.id));
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

            const auto functionId = interpreter->getCallableIdParameter(0);

            scene->setOnPostUpdateCallback(functionId);
        },
        {
            defParameter(
                "function",
                m_runtime->getTypeManager().getCallbackType({}, m_runtime->getVoidType()),
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

            const auto functionId = interpreter->getCallableIdParameter(0);

            scene->setOnUpdate(functionId);
        },
        {
            defParameter(
                "function",
                m_runtime->getTypeManager().getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("Function to execute on each update of the scene")
            )
        },
        m_runtime->getVoidType(),
        NomadDoc("Sets the onUpdate callback for this scene. The callback function is called before updating the entities in the scene.")
    );

    m_runtime->registerNativeFunction(
        "scene.queue",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot trigger event outside of a scene")

            const auto& eventName = interpreter->getStringParameter(1)->getString();
            auto frameCount = interpreter->getIntegerParameter(0);

            scene->scheduleEvent(eventName, frameCount);
        },
        {
            defParameter("frameCount", m_runtime->getIntegerType(), NomadParamDoc("Number of frames to wait before triggering the event.")),
            defParameter("eventName", m_runtime->getStringType(), NomadParamDoc("Name of the event to trigger."))
        },
        m_runtime->getVoidType(),
        NomadDoc("Trigger an event after a specified number of frames.")
    );

    m_runtime->registerNativeFunction(
        "scene.trigger",
        [this](VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot trigger event outside of a scene")

            const auto eventId = EventId{interpreter->getIdParameter(0)};

            std::vector<RuntimeValue> args;

            const auto* event = m_runtime->getEventDefinition(eventId);

            if (!event) {
                log::error(NomadString("Invalid event id for scene.trigger: ") + toString(eventId.id));
                return;
            }

            for (std::size_t i = 0; i < event->parameters.size(); ++i) {
                const auto& parameter = event->parameters[i];

                const auto type = m_runtime->getType(parameter.typeId);

                RuntimeValue value;

                if (type) {
                    (*type)->copyValue(&interpreter->peekStack(static_cast<NomadIndex>(i) + 1), &value);
                } else {
                    log::error("Invalid type id for event parameter: " + toString(parameter.typeId.id));
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

            const auto eventId = EventId{interpreter->getIdParameter(0)};
            const auto layerId = interpreter->getIntegerParameter(1);

            std::vector<RuntimeValue> args;

            const auto* event = m_runtime->getEventDefinition(eventId);

            if (!event) {
                log::error(NomadString("Invalid event id for scene.trigger.layer: ") + toString(eventId.id));
                return;
            }

            for (std::size_t i = 0; i < event->parameters.size(); ++i) {
                const auto& parameter = event->parameters[i];

                const auto type = m_runtime->getType(parameter.typeId);

                RuntimeValue value;

                if (type) {
                    (*type)->copyValue(&interpreter->peekStack(static_cast<NomadIndex>(i) + 2), &value);
                } else {
                    log::error("Invalid type id for event parameter: " + toString(parameter.typeId.id));
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
        "select",
        [this](VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot load input mapping outside of a scene")

            auto predicateId = interpreter->getCallableIdParameter(0);

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
                "predicate", m_runtime->getTypeManager().getPredicateType(),
                NomadParamDoc("The predicate used to select other entities")
            ),
        },
        m_runtime->getIntegerType(),
        NomadDoc("Select entities in the same layer as the `this` entity that match the predicate.")
    );

    m_runtime->registerNativeFunction(
        "select.all",
        [this](const VirtualMachine* interpreter) {
            CHECK_SCENE_NOT_NULL("Cannot load input mapping outside of a scene")

            auto predicateId = interpreter->getCallableIdParameter(0);

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
                "predicate", m_runtime->getTypeManager().getPredicateType(),
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

            const auto& name = interpreter->getStringParameter(0)->getString();

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
            defParameter("name", m_runtime->getStringType(), NomadParamDoc("The name of the entity to select")),
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
