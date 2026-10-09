// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".animation.direction",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set animation direction of null entity")

        auto animationDirection = value.getStringValue();

        entity->setAnimationDirection(animationDirection);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get animation direction of null entity")

        value.setStringValue(entity->getAnimationDirection());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_STRING)
    },
    m_runtime->getStringType(),
    NomadDoc("The animation direction of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".animation.name",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set animation name of null entity")

        auto animationName = value.getStringValue();

        entity->setAnimationName(animationName);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get animation name of null entity")

        value.setStringValue(entity->getAnimationName());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_STRING)
    },
    m_runtime->getStringType(),
    NomadDoc("The animation name of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".animation.repeat",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set animation repeat of null entity")

        auto animationRepeat = value.getBooleanValue();

        entity->setAnimationRepeat(animationRepeat);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get animation repeat of null entity")

        value.setIntegerValue(entity->getAnimationRepeat());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_BOOLEAN)
    },
    m_runtime->getBooleanType(),
    NomadDoc("The animation speed of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".animation.reverse",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set animation reverse of null entity")

        auto animationReverse = value.getBooleanValue();

        entity->setAnimationReverse(animationReverse);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get animation reverse of null entity")

        value.setIntegerValue(entity->getAnimationReverse());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_BOOLEAN)
    },
    m_runtime->getBooleanType(),
    NomadDoc("Should the animation be played in reversed order.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".animation.speed",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set animation speed of null entity")

        auto animationSpeed = value.getIntegerValue();

        entity->setAnimationDuration(animationSpeed);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get animation speed of null entity")

        value.setIntegerValue(entity->getAnimationDuration());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_INTEGER)
    },
    m_runtime->getIntegerType(),
    NomadDoc("The animation speed of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".animation.variant",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set animation variant of null entity")

        auto animationVariant = value.getStringValue();

        entity->setAnimationVariant(animationVariant);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get animation variant of null entity")

        value.setStringValue(entity->getAnimationVariant());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_STRING)
    },
    m_runtime->getStringType(),
    NomadDoc("The animation variant of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".body.height",
    nullptr,
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get body height of null entity")

        value.setFloatValue(entity->getBodyHeight());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The body height of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".body.shape",
    nullptr,
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get body shape of null entity")

        value.setIntegerValue(static_cast<NomadInteger>(entity->getBodyShape()));

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_INTEGER)
    },
    m_runtime->getIntegerType(),
    NomadDoc("The body shape of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".body.width",
    nullptr,
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get body width of null entity")

        value.setFloatValue(entity->getBodyWidth());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The body width of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".collisionMask",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set collision mask of null entity")

        auto mask = value.getIntegerValue();
        entity->setCollisionMask(mask);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get collision mask of null entity")

        value.setIntegerValue(entity->getCollisionMask());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_INTEGER)
    },
    m_runtime->getIntegerType(),
    NomadDoc("The collision mask of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".distanceMoved",
    nullptr,
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set distanceMoved of null entity")

        value.setFloatValue(entity->getDistanceMoved());

        END_ENTITY_BLOCK
    },
    m_runtime->getFloatType(),
    NomadDoc("The distance the entity has moved since the last frame.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".hasMoved",
    nullptr,
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get hasMoved of null entity")

        value.setBooleanValue(entity->hasMoved());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_BOOLEAN)
    },
    m_runtime->getBooleanType(),
    NomadDoc("Whether the entity has moved since the last frame.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".height",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set height of null entity")

        auto height = value.getFloatValue();
        entity->setHeight(height);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get height of null entity")

        value.setFloatValue(entity->getHeight());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The height of an entity")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".layer",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set layer position of null entity")

        auto layer = value.getIntegerValue();
        entity->setLayer(layer);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get layer of null entity")

        value.setIntegerValue(entity->getLayer());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_INTEGER)
    },
    m_runtime->getIntegerType(),
    NomadDoc("The layer position of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".mask",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set mask of null entity")

        auto mask = value.getIntegerValue();
        entity->setMask(mask);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get mask of null entity")

        value.setIntegerValue(entity->getMask());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_INTEGER)
    },
    m_runtime->getIntegerType(),
    NomadDoc("The mask of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".name",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set the name of a null entity")

        auto name = value.getStringValue();
        entity->setName(name);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get the name of a null entity")

        value.setStringValue(entity->getName());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_STRING)
    },
    m_runtime->getStringType(),
    NomadDoc("The name of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".opacity",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set opacity of null entity")

        auto opacity = value.getFloatValue();

        entity->setOpacity(opacity);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get opacity of null entity")

        value.setFloatValue(entity->getOpacity());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The opacity of the entity between 0.0 and 1.0")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".previousX",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set previous.x of null entity")

        auto floatValue = value.getFloatValue();
        entity->setPreviousX(floatValue);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get previous.x of null entity")

        value.setFloatValue(entity->getPreviousPosition().getX());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The previous x position of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".previousY",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set previous.y of null entity")

        auto floatValue = value.getFloatValue();
        entity->setPreviousY(floatValue);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get previous.y of null entity")

        value.setFloatValue(entity->getPreviousPosition().getY());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The previous y position of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".radius",
    nullptr,
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get body radius of null entity")

        value.setFloatValue(entity->getBodyRadius());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The body radius of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".sensorMask",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set sensor mask of null entity")

        auto sensorMask = value.getIntegerValue();
        entity->setSensorMask(sensorMask);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get sensor mask of null entity")

        value.setIntegerValue(entity->getSensorMask());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_BOOLEAN)
    },
    m_runtime->getIntegerType(),
    NomadDoc("Determine the sensor mask of the entity. Default is 0 (no sensor).")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".sprite.height",
    nullptr,
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to read height of null entity")

        auto sprite = entity->getSprite();

        if (sprite) {
            value.setIntegerValue(sprite->getHeight());
        } else {
            log::warning("Attempted to read sprite height of null entity");
            value.setIntegerValue(0);
        }

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getIntegerType(),
    NomadDoc("Read the height of the sprite of an entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".sprite.name",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set sprite.name of null entity")

        auto spriteName = value.getStringValue();
        entity->setSpriteName(spriteName);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to read sprite.name of null entity")

        value.setStringValue(entity->getSpriteName());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getStringType(),
    NomadDoc("The name of an entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".sprite.width",
    nullptr,
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to read width of null entity")

        auto sprite = entity->getSprite();

        if (sprite) {
            value.setIntegerValue(sprite->getWidth());
        } else {
            log::warning("Attempted to read width of null sprite");
            value.setIntegerValue(0);
        }

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getIntegerType(),
    NomadDoc("Read the width of the sprite of an entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".sprite.x",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set sprite.x position of null entity")

        auto floatValue = value.getFloatValue();
        entity->setSpriteX(floatValue);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to read sprite.x of null entity")

        value.setFloatValue(entity->getSpriteX());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("Read the x anchor (offset) of the sprite of an entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".sprite.y",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set sprite.y position of null entity")

        auto floatValue = value.getFloatValue();
        entity->setSpriteY(floatValue);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to read sprite.y of null entity")

        value.setFloatValue(entity->getSpriteY());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("Read the y anchor (offset) of the sprite of an entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".text.alignment",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set text alignment of null entity")

        auto alignment = static_cast<Alignment>(value.getIntegerValue());

        entity->setTextAlignment(alignment);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get text alignment of null entity")

        value.setIntegerValue(static_cast<int>(entity->getTextAlignment()));

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_INTEGER)
    },
    m_runtime->getIntegerType(),
    NomadDoc("The point of the text anchored at the entity position plus text.x and text.y. "
        "topLeft anchors the top-left corner, centerMiddle the center, and bottomRight the bottom-right corner.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".text.color",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set text color of null entity")

        auto rgba = Rgba(value.getIntegerValue());

        entity->setTextColor(Color(rgba));

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get text color of null entity")

        auto color = entity->getTextColor();

        value.setIntegerValue(color.rgba);

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_INTEGER)
    },
    m_runtime->getIntegerType(),
    NomadDoc("The text color of an entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".text.font",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set font of null entity")

        auto fontId = static_cast<NomadId>(value.getIntegerValue());
        entity->setFontById(fontId);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get font of null entity")

        value.setIdValue(entity->getFontId());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_INTEGER)
    },
    m_runtime->getIntegerType(),
    NomadDoc("The font of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".text.height",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set text height of null entity")

        entity->setTextHeight(value.getFloatValue());

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get text height of null entity")

        value.setFloatValue(entity->getTextHeight());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The maximum height of the text of an entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".text.lineSpacing",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set text line spacing of null entity")

        entity->setTextLineSpacing(value.getFloatValue());

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get text line spacing of null entity")

        value.setFloatValue(entity->getTextLineSpacing());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The text line spacing of an entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".text.value",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set text of null entity")

        auto text = value.getStringValue();

        entity->setText(text);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get text of null entity")

        value.setStringValue(entity->getText());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_STRING)
    },
    m_runtime->getStringType(),
    NomadDoc("Set the text of the sprite of an entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".text.width",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set text width position of null entity")

        entity->setTextWidth(value.getFloatValue());

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get text width position of null entity")

        value.setFloatValue(entity->getTextWidth());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The width of the text of an entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".text.x",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set text x position of null entity")

        entity->setTextX(value.getFloatValue());

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get text x position of null entity")

        value.setFloatValue(entity->getTextX());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The horizontal offset of the text anchor from the entity position, applied once.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".text.y",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set text y position of null entity")

        entity->setTextY(value.getFloatValue());

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get text y position of null entity")

        value.setFloatValue(entity->getTextY());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The vertical offset of the text anchor from the entity position, applied once.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".velocity.x",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set velocity.x of null entity")

        auto floatValue = value.getFloatValue();
        entity->setVelocityX(floatValue);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get velocity.x of null entity")

        value.setFloatValue(entity->getVelocity().getX());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The x velocity of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".velocity.y",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set velocity.y of null entity")

        auto floatValue = value.getFloatValue();
        entity->setVelocityY(floatValue);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get velocity.y of null entity")

        value.setFloatValue(entity->getVelocity().getY());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The y velocity of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".visible",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set visible of null entity")

        auto visible = value.getBooleanValue();
        entity->setVisible(visible);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get visible of null entity")

        value.setBooleanValue(entity->isVisible());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_BOOLEAN)
    },
    m_runtime->getBooleanType(),
    NomadDoc("Determine if the entity is visible. Default is true.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".width",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set the width of a null entity")

        auto width = value.getFloatValue();

        entity->setWidth(width);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get the width of a null entity")

        value.setFloatValue(entity->getWidth());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The width of an entity")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".x",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Cannot set x position on a null entity")

        auto floatValue = value.getFloatValue();

        entity->setX(floatValue);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get x position of null entity")

        value.setFloatValue(entity->getX());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The x position of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".y",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set y position of null entity")

        auto floatValue = value.getFloatValue();
        entity->setY(floatValue);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get y position of null entity")

        value.setFloatValue(entity->getY());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The y position of the entity.")
);

m_runtime->registerDynamicVariable(
    VARIABLE_NAME_PREFIX ".z",
    [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
        BEGIN_ENTITY_BLOCK("Attempted to set z position of null entity")

        auto floatValue = value.getFloatValue();
        entity->setZ(floatValue);

        END_ENTITY_BLOCK
    },
    [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
        BEGIN_SINGLE_ENTITY_BLOCK("Attempted to get z position of null entity")

        value.setFloatValue(entity->getZ());

        END_SINGLE_ENTITY_BLOCK(NOMAD_DEFAULT_FLOAT)
    },
    m_runtime->getFloatType(),
    NomadDoc("The z position (draw order) of the entity.")
);
