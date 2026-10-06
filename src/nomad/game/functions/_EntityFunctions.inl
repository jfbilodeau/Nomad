// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

BEGIN_ENTITY_FUNCTION(addSystem)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto functionId = interpreter->getIdParameter(0);

        BEGIN_ENTITY_BLOCK()

        if (functionId == NOMAD_INVALID_ID) {
            log::error("Function for system not found");
            return;
        }

        entity->getScene()->getGame()->executeFunction(functionId, entity->getScene(), entity); // Test function existence.

        END_ENTITY_BLOCK()
    }, {
        defParameter("function", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Function to execute as a system."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Add a system function to the entity. The function will be executed every frame (update).")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(body.circle)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto bodyType = static_cast<BodyType>(interpreter->getIntegerParameter(0));
        auto radius = interpreter->getFloatParameter(1);

        BEGIN_ENTITY_BLOCK()

        entity->setCircleBody(bodyType, radius);

        END_ENTITY_BLOCK()
    }, {
        defParameter("bodyType", m_runtime->getIntegerType(), NomadParamDoc("Body type.")),
        defParameter("radius", m_runtime->getFloatType(), NomadParamDoc("Radius of the circle body.")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Set the circle body of the entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(body.none)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* /*interpreter*/) {
        BEGIN_ENTITY_BLOCK()

        entity->setNoBody();

        END_ENTITY_BLOCK()
    },
    {},
    m_runtime->getVoidType(),
    NomadDoc("Set no body on the entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(body.rectangle)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto bodyType = static_cast<BodyType>(interpreter->getIntegerParameter(0));
        auto width = interpreter->getFloatParameter(1);
        auto height = interpreter->getFloatParameter(2);

        BEGIN_ENTITY_BLOCK()

        entity->setRectangleBody(bodyType, width, height);

        END_ENTITY_BLOCK()
    }, {
        defParameter("bodyType", m_runtime->getIntegerType(), NomadParamDoc("Body type.")),
        defParameter("width", m_runtime->getFloatType(), NomadParamDoc("Width of the rectangle body.")),
        defParameter("height", m_runtime->getFloatType(), NomadParamDoc("Height of the rectangle body.")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Set the rectangle body of the entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(getCollidingMask)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        BEGIN_SINGLE_ENTITY_BLOCK()

        auto scene = entity->getScene();

        auto result = scene->getMaskAtEntity(entity);

        interpreter->setIntegerResult(result);

        END_SINGLE_ENTITY_BLOCK(0)
    },
    {},
    m_runtime->getIntegerType(),
    NomadDoc("Get the collision mask at the entity's position.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(getMaskAt)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto x = interpreter->getFloatParameter(0);
        auto y = interpreter->getFloatParameter(1);

        BEGIN_SINGLE_ENTITY_BLOCK()

        auto scene = entity->getScene();

        auto result = scene->getMaskAtEntity(entity, {x, y});

        interpreter->setIntegerResult(result);

        END_SINGLE_ENTITY_BLOCK(0)
    },
    {
        defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X coordinate relative to entity")),
        defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y coordinate relative to entity")),
    },
    m_runtime->getIntegerType(),
    NomadDoc("Get the tile mask for the entity at the specified relative position.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(moveTo)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interperter) {
        auto x = interperter->getFloatParameter(0);
        auto y = interperter->getFloatParameter(1);
        auto speed = interperter->getFloatParameter(2);

        BEGIN_ENTITY_BLOCK()

        entity->moveTo(x, y, speed, NOMAD_INVALID_ID);

        END_ENTITY_BLOCK()
    },
    {
        defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X coordinate of the destination.")),
        defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y coordinate of the destination.")),
        defParameter("speed", m_runtime->getFloatType(), NomadParamDoc("Speed in pixels per seconds to move to the destination.")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Move the entity to a specified location.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(moveToThen)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto x = interpreter->getFloatParameter(0);
        auto y = interpreter->getFloatParameter(1);
        auto speed = interpreter->getFloatParameter(2);
        auto functionId = interpreter->getIdParameter(3);

        auto closure = interpreter->createClosure(m_runtime.get(), functionId);

        BEGIN_ENTITY_BLOCK()

        entity->moveTo(x, y, speed, std::move(closure));

        END_ENTITY_BLOCK()
    },
    {
        defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X coordinate of the destination.")),
        defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y coordinate of the destination.")),
        defParameter("speed", m_runtime->getFloatType(), NomadParamDoc("Speed in pixels per seconds to move to the destination.")),
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function or function."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Move the entity to a specified location then execute a function when destination is reached.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(oldon)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        NomadString eventName = interpreter->getParameter(0).getStringValue();
        auto functionId = interpreter->getParameter(1).getIdValue();
        auto scene = getCurrentContext()->getScene();

        BEGIN_ENTITY_BLOCK()

        if (functionId == NOMAD_INVALID_ID) {
            log::error("Function or function for event '" + eventName + "' not found");
            return;
        }

        scene->registerEntityEvent(eventName, entity->getId(), functionId);

        END_ENTITY_BLOCK()
    }, {
        defParameter("eventName", m_runtime->getStringRefType(), NomadParamDoc("Name of the event to listen to.")),
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function or function."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Assign a function or function to an event for this entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(oldtrigger)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto eventName = interpreter->getStringParameter(0);

        auto scene = getCurrentContext()->getScene();

        BEGIN_ENTITY_BLOCK()

        scene->getGame()->triggerEntityEvent(
            scene->getId(),
            entity->getId(),
            eventName
        );

        END_ENTITY_BLOCK()
    }, {
        defParameter("eventName", m_runtime->getStringRefType(), NomadParamDoc("Name of the event to trigger.")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Trigger an event for this entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(on)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto eventId = interpreter->getParameter(0).getIdValue();
        auto functionId = interpreter->getParameter(1).getIdValue();

        auto closure = interpreter->createClosure(m_runtime.get(), functionId);

        BEGIN_ENTITY_BLOCK()

        entity->registerEvent(eventId, std::move(closure));

        END_ENTITY_BLOCK()
    }, {
        defParameter("event", m_runtime->getEventCallbackType(), NomadParamDoc("The event to register for.")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Assign a function or function to an event for this entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(onBeginCollision)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto functionId = interpreter->getIdParameter(0);

        BEGIN_ENTITY_BLOCK()

        if (functionId == NOMAD_INVALID_ID) {
            log::error("Function for begin collision event not found");
            return;
        }

        entity->setOnBeginCollision(functionId);

        END_ENTITY_BLOCK()
    }, {
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function or function."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Assign a function to the begin collision event for this entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(onBeginCollisionWith)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto mask = interpreter->getIntegerParameter(0);
        auto functionId = interpreter->getIdParameter(1);

        BEGIN_ENTITY_BLOCK()

        if (functionId == NOMAD_INVALID_ID) {
            log::error("Function for onBeginCollisionWith not found");
            return;
        }

        entity->setOnBeginCollisionWith(mask, functionId);

        END_ENTITY_BLOCK()
    }, {
        defParameter("mask", m_runtime->getIntegerType(), NomadParamDoc("Collision mask.")),
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function or function."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Assign a function to the begin collision event with a mask.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(onEndCollision)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto functionId = interpreter->getIdParameter(0);

        BEGIN_ENTITY_BLOCK()

        if (functionId == NOMAD_INVALID_ID) {
            log::error("Function or function for collision end event not found");
            return;
        }

        entity->setOnEndCollision(functionId);

        END_ENTITY_BLOCK()
    }, {
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function or function."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Assign a function or function to the collision end event.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(onEndCollisionWith)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto mask = interpreter->getIntegerParameter(0);
        auto functionId = interpreter->getIdParameter(1);

        BEGIN_ENTITY_BLOCK()

        if (functionId == NOMAD_INVALID_ID) {
            log::error("Function or function for end collision with event not found");
            return;
        }

        entity->setOnEndCollisionWith(mask, functionId);

        END_ENTITY_BLOCK()
    }, {
        defParameter("mask", m_runtime->getIntegerType(), NomadParamDoc("Collision mask.")),
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function or function."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Assign a function or function to the end collision with event.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(onPress)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        BEGIN_ENTITY_BLOCK()

        const auto actionName = interpreter->getStringParameter(0);
        const auto functionId = interpreter->getIdParameter(1);
        const auto entityId = entity->getId();

        getCurrentContext()->getScene()->addActionPressed(
            actionName,
            functionId,
            entityId
        );

        END_ENTITY_BLOCK()
    }, {
        defParameter("actionName", m_runtime->getStringRefType(), NomadParamDoc("Name of the action to listen to.")),
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function"))
    },
    m_runtime->getVoidType(),
    NomadDoc("Assign a function or function to the pressed event for this entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(onRelease)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto actionName = interpreter->getStringParameter(0);
        auto functionId = interpreter->getIdParameter(1);

        BEGIN_ENTITY_BLOCK()

        getCurrentContext()->getScene()->addActionReleased(
            actionName,
            functionId,
            entity->getId()
        );

        END_ENTITY_BLOCK()
    }, {
        defParameter("actionName", m_runtime->getStringRefType(), NomadParamDoc("Name of the action to listen to.")),
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function or function."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Assign a function or function to the release event for this entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(onTimer)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        const auto frameCount = interpreter->getIntegerParameter(0);
        const auto repeat = interpreter->getBooleanParameter(1);
        const auto functionId = interpreter->getIdParameter(2);

        const auto scene = getCurrentContext()->getScene();
        const auto sceneId = scene->getId();
        const auto game = scene->getGame();

        BEGIN_ENTITY_BLOCK()

        if (functionId == NOMAD_INVALID_ID) {
            log::error("Function for onTimer not found");
            return;
        }

        const auto entityId = entity->getId();

        game->scheduleEntityCallback(sceneId, entityId, frameCount, repeat, functionId);

        END_ENTITY_BLOCK()
    }, {
        defParameter("frameCount", m_runtime->getIntegerType(), NomadParamDoc("Number of frames to wait before triggering the timer.")),
        defParameter("repeat", m_runtime->getBooleanType(), NomadParamDoc("Whether the timer should repeat.")),
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function or function.")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Assign a function or function to be called after a timer expires.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(onUpdate)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto functionId = interpreter->getIdParameter(0);

        BEGIN_ENTITY_BLOCK()

        if (functionId == NOMAD_INVALID_ID) {
            log::error("Function for update event not found");
            return;
        }

        entity->setOnUpdate(functionId);

        END_ENTITY_BLOCK()
    }, {
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function or function."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Assign a function to the update event for this entity. Called every frame (update).")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(playAnimation)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto variant = interpreter->getStringParameter(0);
        auto duration = interpreter->getIntegerParameter(1);
        auto repeat = interpreter->getBooleanParameter(2);
        auto reverse = interpreter->getBooleanParameter(3);

        BEGIN_ENTITY_BLOCK()

        entity->playAnimation(
            variant,
            duration,
            repeat,
            reverse
        );

        END_ENTITY_BLOCK()
    }, {
        defParameter("name", m_runtime->getStringRefType(), NomadParamDoc("Name of the animation to set.")),
        defParameter("duration", m_runtime->getIntegerType(), NomadParamDoc("Number of frames to display an animation frame.")),
        defParameter("repeat", m_runtime->getBooleanType(), NomadParamDoc("Whether to repeat the animation.")),
        defParameter("reverse", m_runtime->getBooleanType(), NomadParamDoc("Whether to reverse the animation.")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Play an animation for the entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(playAnimationAndThen)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto variant = interpreter->getStringParameter(0);
        auto duration = interpreter->getIntegerParameter(1);
        auto repeat = interpreter->getBooleanParameter(2);
        auto reverse = interpreter->getBooleanParameter(3);
        auto functionId = interpreter->getIdParameter(4);

        auto closure = interpreter->createClosure(m_runtime.get(), functionId);

        BEGIN_ENTITY_BLOCK()

        entity->playAnimation(
            variant,
            duration,
            repeat,
            reverse,
            std::move(closure)
        );

        END_ENTITY_BLOCK()
    }, {
        defParameter("variant", m_runtime->getStringRefType(), NomadParamDoc("Variant name of the animation to set.")),
        defParameter("duration", m_runtime->getIntegerType(), NomadParamDoc("Number of frames to display an animation frame.")),
        defParameter("repeat", m_runtime->getBooleanType(), NomadParamDoc("Whether to repeat the animation. Callback executed every time the animation ends.")),
        defParameter("reverse", m_runtime->getBooleanType(), NomadParamDoc("Whether to reverse the animation.")),
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function or function."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Play an animation for the entity, then execute a function or function once the animation completes.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(removeAllSystems)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* /*interpreter*/) {
        BEGIN_ENTITY_BLOCK()

        entity->removeAllSystems();

        END_ENTITY_BLOCK()
    },
    {},
    m_runtime->getVoidType(),
    NomadDoc("Remove all system functions from the entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(removeSelf)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* /*interpreter*/) {
        BEGIN_ENTITY_BLOCK()

        auto scene = entity->getScene();

        //scene->removeEntity(entity);
        scene->getGame()->removeEntityFromScene(scene->getId(), entity->getId());

        END_ENTITY_BLOCK()
    }, { },
    m_runtime->getVoidType(),
    NomadDoc("Remove this entity from the scene.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(removeSystem)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto registrationId = interpreter->getStringParameter(0);

        BEGIN_ENTITY_BLOCK()

        entity->removeSystem(registrationId);

        END_ENTITY_BLOCK()
    }, {
        defParameter("registrationId", m_runtime->getStringRefType(), NomadParamDoc("Id of the system to remove from the entity."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Remove a system function from the entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(repositionOnAnchor)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto anchorX = interpreter->getFloatParameter(0);
        auto anchorY = interpreter->getFloatParameter(1);

        BEGIN_ENTITY_BLOCK()

        auto newX = entity->getX() + anchorX;
        auto newY = entity->getY() + anchorY;

        entity->setLocation(newX, newY);
        // entity->setTextPosition(anchorX, anchorY);
        entity->setSpriteAnchor(anchorX, anchorY);

        END_ENTITY_BLOCK()
    },
    {
        defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X coordinate")),
        defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y coordinate")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Reposition the entity based on the new sprite anchor.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(setLocation)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto x = interpreter->getFloatParameter(0);
        auto y = interpreter->getFloatParameter(1);

        BEGIN_ENTITY_BLOCK()

        entity->setLocation(x, y);

        END_ENTITY_BLOCK()
    }, {
        defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X coordinate")),
        defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y coordinate")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Set the x,y location of the entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(setSprite)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        NomadString spriteName = interpreter->getStringParameter(0);

        auto sprite = m_resourceManager->getSprites()->getSpriteByName(spriteName);

        if (sprite == nullptr) {
            log::error("Sprite '" + spriteName + "' not found");
            return;
        }

        BEGIN_ENTITY_BLOCK()

        entity->setSprite(sprite);

        END_ENTITY_BLOCK()
    }, {
        defParameter("name", m_runtime->getStringRefType(), NomadParamDoc("Name of the sprite to set.")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Set the sprite of the entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(setSpriteAnchor)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto x = interpreter->getFloatParameter(0);
        auto y = interpreter->getFloatParameter(1);

        BEGIN_ENTITY_BLOCK()

        entity->setLocation(entity->getX() - x, entity->getY() - y);
        entity->setSpriteAnchor(x, y);

        END_ENTITY_BLOCK()
    }, {
        defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X coordinate")),
        defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y coordinate")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Set the anchor point of the sprite of the entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(setText)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto fontId = interpreter->getIdParameter(0);
        auto text = interpreter->getStringParameter(1);

        BEGIN_ENTITY_BLOCK()

        entity->setFontById(fontId);
        entity->setText(text);

        END_ENTITY_BLOCK()
    }, {
        defParameter("fontId", m_runtime->getIntegerType(), NomadParamDoc("ID of the font to use")),
        defParameter("text", m_runtime->getStringRefType(), NomadParamDoc("Text to set")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Set the font and text to display on an entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(systems.afterUpdate)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto systemId = interpreter->getStringParameter(0);
        auto functionId = interpreter->getIdParameter(1);

        BEGIN_ENTITY_BLOCK()

        if (functionId == NOMAD_INVALID_ID) {
            log::error("Function for systems.afterUpdate not found");
            return;
        }

        entity->addSystemAfterUpdate(systemId, functionId);

        END_ENTITY_BLOCK()
    }, {
        defParameter("$function", m_runtime->getFunctionNameType(), NomadParamDoc("ID of the system.")),
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function or function."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Assign a function to be executed after updating this entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(systems.beforeUpdate)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto systemId = interpreter->getStringParameter(0);
        auto functionId = interpreter->getIdParameter(1);

        BEGIN_ENTITY_BLOCK()

        if (functionId == NOMAD_INVALID_ID) {
            log::error("Function for systems.beforeUpdate not found");
            return;
        }

        entity->addSystemBeforeUpdate(systemId, functionId);

        END_ENTITY_BLOCK()
    }, {
        defParameter("$function", m_runtime->getFunctionNameType(), NomadParamDoc("ID of the system.")),
        defParameter("callback", m_runtime->getCallbackType({}, m_runtime->getVoidType()), NomadParamDoc("Callback function or function."))
    },
    m_runtime->getVoidType(),
    NomadDoc("Assign a function to be executed before updating this entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(trigger)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto eventId = interpreter->getIdParameter(0);

        auto scene = getCurrentContext()->getScene();

        std::vector<RuntimeValue> args;

        auto event = m_runtime->getEventDefinition(eventId);

        if (!event) {
            log::error(NomadString("Invalid event id for ") + ENTITY_FUNCTION_NAME + ": " + toString(eventId));

            return;
        }

        for (NomadIndex i = 0; i < event->parameters.size(); ++i) {
            auto& parameter = event->parameters[i];

            auto type = m_runtime->getType(parameter.typeId);

            RuntimeValue value;

            if (type) {
                (*type)->copyValue(interpreter->peekStack(i+1), value); // i+1 to skip the event id
            } else {
                log::error("Invalid type id for event parameter: " + toString(parameter.typeId));
                // Should never happen. Leaving default value (0)
            }

            args.emplace_back(value);
        }

        BEGIN_ENTITY_BLOCK()

        scene->getGame()->dispatchEvent(EventDispatch {
            eventId,
            scene->getId(),
            NOMAD_INVALID_ID,
            entity->getId(),
            0,
            std::move(args),
        });

        END_ENTITY_BLOCK()
    }, {
        defParameter("event", m_runtime->getEventDispatchType(), NomadParamDoc("The event to dispatch (trigger)")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Trigger an event for this entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(velocity.clear)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* /*interpreter*/) {
        BEGIN_ENTITY_BLOCK()

        entity->setVelocity(0, 0);

        END_ENTITY_BLOCK()
    },
    {},
    m_runtime->getVoidType(),
    NomadDoc("Clear the velocity of the entity.")
END_ENTITY_FUNCTION()

BEGIN_ENTITY_FUNCTION(velocity.set)
    [this, ENTITY_FUNCTION_NAME](VirtualMachine* interpreter) {
        auto x = interpreter->getFloatParameter(0);
        auto y = interpreter->getFloatParameter(1);

        BEGIN_ENTITY_BLOCK()

        entity->setVelocity(x, y);

        END_ENTITY_BLOCK()
    }, {
        defParameter("x", m_runtime->getFloatType(), NomadParamDoc("X velocity")),
        defParameter("y", m_runtime->getFloatType(), NomadParamDoc("Y velocity")),
    },
    m_runtime->getVoidType(),
    NomadDoc("Set the velocity of the entity.")
END_ENTITY_FUNCTION()
