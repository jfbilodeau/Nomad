// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

#define CHECK_SCENE_NOT_NULL(message) \
    auto scene = getCurrentContext()->getScene(); \
    if (scene == nullptr) { \
        log::error(message); \
        return; \
    }

void Game::initSceneDynamicVariables() const {
    log::debug("Initializing scene dynamic variables");

    m_runtime->registerDynamicVariable(
        "scene.camera.x",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't set camera x, scene is null")

            const auto cameraX = value.getFloatValue();
            scene->setCameraX(cameraX);
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't get camera x, scene is null")

            const auto cameraX = scene->getCameraX();

            value.setFloatValue(cameraX);
        },
        m_runtime->getFloatType(),
        NomadDoc("The x position of the camera")
    );

    m_runtime->registerDynamicVariable(
        "scene.camera.y",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't set camera y, scene is null")

            const auto cameraY = value.getFloatValue();
            scene->setCameraY(cameraY);
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't get camera y, scene is null")

            const auto cameraY = scene->getCameraY();

            value.setFloatValue(cameraY);
        },
        m_runtime->getFloatType(),
        NomadDoc("The y position of the camera")
    );

    m_runtime->registerDynamicVariable(
        "scene.name",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't set scene name, scene is null")

            const auto sceneName = value.getStringValue();
            scene->setName(sceneName);
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't get scene name, scene is null")

            const auto scene_name = scene->getName();

            value.setStringValue(scene_name);
        },
        m_runtime->getStringType(),
        NomadDoc("The name of the current scene")
    );

    m_runtime->registerDynamicVariable(
        "scene.tilemap.height",
        nullptr,
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't get scene tilemap height, scene is null")

            const auto tilemapHeight = scene->getTileMapHeight();

            value.setIntegerValue(tilemapHeight);
        },
        m_runtime->getIntegerType(),
        NomadDoc("The number of vertical tiles in the tile map")
    );

    m_runtime->registerDynamicVariable(
        "scene.tilemap.height.pixel",
        nullptr,
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't get scene tile map height in pixels because the scene is null")

            const auto tilemapHeight = scene->getTileMapHeight();
            const auto tileHeight = scene->getTileHeight();
            const auto tilemapHeightInPixels = tilemapHeight * tileHeight;

            value.setIntegerValue(tilemapHeightInPixels);
        },
        m_runtime->getIntegerType(),
        NomadDoc("The height of the tile map in pixels")
    );

    m_runtime->registerDynamicVariable(
        "scene.tilemap.width",
        nullptr,
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't get scene tilemap width, scene is null")

            const auto tilemapWidth = scene->getTileMapWidth();

            value.setIntegerValue(tilemapWidth);
        },
        m_runtime->getIntegerType(),
        NomadDoc("The number of horizontal tiles in the tile map")
    );

    m_runtime->registerDynamicVariable(
        "scene.tilemap.width.pixel",
        nullptr,
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't get scene tile map width in pixels, scene is null")

            const auto tilemapWidth = scene->getTileMapWidth();
            const auto tileWidth = scene->getTileWidth();
            const auto tilemapWidthInPixels = tilemapWidth * tileWidth;

            value.setIntegerValue(tilemapWidthInPixels);
        },
        m_runtime->getIntegerType(),
        NomadDoc("The width of the tile map in pixel units")
    );

    m_runtime->registerDynamicVariable(
        "scene.tint",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't set scene tint, scene is null")

            const auto tint = value.getIntegerValue();
            const auto rgba = static_cast<Rgba>(tint);
            scene->setTint(Color(rgba));
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't get scene tint, scene is null")

            const auto tint = scene->getTint();

            value.setIntegerValue(tint.rgba);
        },
        m_runtime->getIntegerType(),
        NomadDoc("The tint of the current scene")
    );

    m_runtime->registerDynamicVariable("scene.z",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't set scene z, scene is null")

            const auto sceneZ = value.getIntegerValue();
            scene->setZ(sceneZ);
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't get scene z, scene is null")

            const auto sceneZ = scene->getZ();

            value.setIntegerValue(sceneZ);
        },
        m_runtime->getIntegerType(),
        NomadDoc("The z-index of the current scene")
    );

    m_runtime->registerDynamicVariable(
        "select.count",
        nullptr,
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            CHECK_SCENE_NOT_NULL("Can't get select count, scene is null")

            const auto count = static_cast<NomadInteger>(getCurrentContext()->getOtherEntities().size());

            value.setIntegerValue(count);
        },
        m_runtime->getIntegerType(),
        NomadDoc("The number of entities currently selected")
    );


}

} // nomad
