// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/WindowMouseObject.hpp>
#include <nomad/game/WindowObject.hpp>
#include <nomad/game/WindowResolutionObject.hpp>

#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

void Game::initWindowProperties() {
    const auto typeId = m_runtime->getTypeId("game.Window");
    const auto mouseTypeId = m_runtime->getTypeId("game.WindowMouse");
    const auto resolutionTypeId = m_runtime->getTypeId("game.WindowResolution");
    const auto fpsId = m_runtime->registerNativeClassProperty(typeId, {
        .valueType = m_runtime->getIntegerType(),
        .name = "fps",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            value.setIntegerValue(machine->getThisAs<WindowObject>()->getGame().m_fps);
        },
        .setter = [](VirtualMachine* machine, const RuntimeValue& value) {
            machine->getThisAs<WindowObject>()->getGame().m_fps = static_cast<int>(value.getIntegerValue());
        },
        .documentation = NomadDoc("The frames per second (FPS) of the game.")
    });
    const auto focusId = m_runtime->registerNativeClassProperty(typeId, {
        .valueType = m_runtime->getBooleanType(),
        .name = "hasFocus",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            const auto& game = machine->getThisAs<WindowObject>()->getGame();
            value.setBooleanValue((SDL_GetWindowFlags(game.m_window) & SDL_WINDOW_INPUT_FOCUS) != 0);
        },
        .setter = {},
        .documentation = NomadDoc("Whether the game window currently has input focus.")
    });
    const auto heightId = m_runtime->registerNativeClassProperty(typeId, {
        .valueType = m_runtime->getIntegerType(),
        .name = "height",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            value.setIntegerValue(machine->getThisAs<WindowObject>()->getGame().m_windowSize.getY());
        },
        .setter = [](VirtualMachine* machine, const RuntimeValue& value) {
            auto& game = machine->getThisAs<WindowObject>()->getGame();
            game.setWindowSize(game.m_windowSize.getX(), static_cast<int>(value.getIntegerValue()));
        },
        .documentation = NomadDoc("The height of the game window.")
    });
    const auto fullscreenId = m_runtime->registerNativeClassProperty(typeId, {
        .valueType = m_runtime->getBooleanType(),
        .name = "isFullScreen",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            const auto& game = machine->getThisAs<WindowObject>()->getGame();
            value.setBooleanValue((SDL_GetWindowFlags(game.m_window) & SDL_WINDOW_FULLSCREEN) != 0);
        },
        .setter = {},
        .documentation = NomadDoc("Whether the game window is currently in full-screen mode.")
    });
    const auto maximizedId = m_runtime->registerNativeClassProperty(typeId, {
        .valueType = m_runtime->getBooleanType(),
        .name = "isMaximized",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            const auto& game = machine->getThisAs<WindowObject>()->getGame();
            value.setBooleanValue((SDL_GetWindowFlags(game.m_window) & SDL_WINDOW_MAXIMIZED) != 0);
        },
        .setter = {},
        .documentation = NomadDoc("Whether the game window is currently maximized.")
    });
    const auto minimizedId = m_runtime->registerNativeClassProperty(typeId, {
        .valueType = m_runtime->getBooleanType(),
        .name = "isMinimized",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            const auto& game = machine->getThisAs<WindowObject>()->getGame();
            value.setBooleanValue((SDL_GetWindowFlags(game.m_window) & SDL_WINDOW_MINIMIZED) != 0);
        },
        .setter = {},
        .documentation = NomadDoc("Whether the game window is currently minimized.")
    });
    const auto mouseId = m_runtime->registerNativeClassProperty(typeId, {
        .valueType = m_runtime->getTypeByName("game.WindowMouse"),
        .name = "mouse",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            auto* mouse = machine->getThisAs<WindowObject>()->getMouse();
            mouse->addRef();
            value.setObjectValue(mouse);
        },
        .setter = {},
        .documentation = NomadDoc("The live mouse position and movement for this window.")
    });
    const auto resolutionId = m_runtime->registerNativeClassProperty(typeId, {
        .valueType = m_runtime->getTypeByName("game.WindowResolution"),
        .name = "resolution",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            auto* resolution = machine->getThisAs<WindowObject>()->getResolution();
            resolution->addRef();
            value.setObjectValue(resolution);
        },
        .setter = {},
        .documentation = NomadDoc("The live logical resolution of this window.")
    });
    const auto titleId = m_runtime->registerNativeClassProperty(typeId, {
        .valueType = m_runtime->getStringType(),
        .name = "title",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            const auto& game = machine->getThisAs<WindowObject>()->getGame();
            value.setStringValue(SDL_GetWindowTitle(game.m_window));
        },
        .setter = [](VirtualMachine* machine, const RuntimeValue& value) {
            machine->getThisAs<WindowObject>()->getGame().setWindowTitle(value.getStringObject()->getString());
        },
        .documentation = NomadDoc("The title of the game window.")
    });
    const auto widthId = m_runtime->registerNativeClassProperty(typeId, {
        .valueType = m_runtime->getIntegerType(),
        .name = "width",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            value.setIntegerValue(machine->getThisAs<WindowObject>()->getGame().m_windowSize.getX());
        },
        .setter = [](VirtualMachine* machine, const RuntimeValue& value) {
            auto& game = machine->getThisAs<WindowObject>()->getGame();
            game.setWindowSize(static_cast<int>(value.getIntegerValue()), game.m_windowSize.getY());
        },
        .documentation = NomadDoc("The width of the game window.")
    });

    const auto mouseXId = m_runtime->registerNativeClassProperty(mouseTypeId, {
        .valueType = m_runtime->getFloatType(),
        .name = "x",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            value.setFloatValue(machine->getThisAs<WindowMouseObject>()->getGame().m_mousePosition.getX());
        },
        .setter = [](VirtualMachine* machine, const RuntimeValue& value) {
            machine->getThisAs<WindowMouseObject>()->getGame().m_mousePosition.setX(value.getFloatValue());
        },
        .documentation = NomadDoc("The mouse x position, not adjusted to the logical resolution.")
    });
    const auto mouseYId = m_runtime->registerNativeClassProperty(mouseTypeId, {
        .valueType = m_runtime->getFloatType(),
        .name = "y",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            value.setFloatValue(machine->getThisAs<WindowMouseObject>()->getGame().m_mousePosition.getY());
        },
        .setter = [](VirtualMachine* machine, const RuntimeValue& value) {
            machine->getThisAs<WindowMouseObject>()->getGame().m_mousePosition.setY(value.getFloatValue());
        },
        .documentation = NomadDoc("The mouse y position, not adjusted to the logical resolution.")
    });
    const auto deltaXId = m_runtime->registerNativeClassProperty(mouseTypeId, {
        .valueType = m_runtime->getFloatType(),
        .name = "deltaX",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            const auto& game = machine->getThisAs<WindowMouseObject>()->getGame();
            value.setFloatValue(game.m_mouseLastPosition.getX() - game.m_mousePosition.getX());
        },
        .setter = {},
        .documentation = NomadDoc("The change in mouse x position since the last frame.")
    });
    const auto deltaYId = m_runtime->registerNativeClassProperty(mouseTypeId, {
        .valueType = m_runtime->getFloatType(),
        .name = "deltaY",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            const auto& game = machine->getThisAs<WindowMouseObject>()->getGame();
            value.setFloatValue(game.m_mouseLastPosition.getY() - game.m_mousePosition.getY());
        },
        .setter = {},
        .documentation = NomadDoc("The change in mouse y position since the last frame.")
    });
    const auto resolutionHeightId = m_runtime->registerNativeClassProperty(resolutionTypeId, {
        .valueType = m_runtime->getIntegerType(),
        .name = "height",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            value.setIntegerValue(machine->getThisAs<WindowResolutionObject>()->getGame().m_resolution.getY());
        },
        .setter = [](VirtualMachine* machine, const RuntimeValue& value) {
            auto& game = machine->getThisAs<WindowResolutionObject>()->getGame();
            game.setResolution(game.m_resolution.getX(), static_cast<int>(value.getIntegerValue()));
        },
        .documentation = NomadDoc("The vertical logical resolution of the game.")
    });
    const auto resolutionWidthId = m_runtime->registerNativeClassProperty(resolutionTypeId, {
        .valueType = m_runtime->getIntegerType(),
        .name = "width",
        .getter = [](VirtualMachine* machine, RuntimeValue& value) {
            value.setIntegerValue(machine->getThisAs<WindowResolutionObject>()->getGame().m_resolution.getX());
        },
        .setter = [](VirtualMachine* machine, const RuntimeValue& value) {
            auto& game = machine->getThisAs<WindowResolutionObject>()->getGame();
            game.setResolution(static_cast<int>(value.getIntegerValue()), game.m_resolution.getY());
        },
        .documentation = NomadDoc("The horizontal logical resolution of the game.")
    });

    if (
        fpsId.isInvalid() ||
        focusId.isInvalid() ||
        heightId.isInvalid() ||
        fullscreenId.isInvalid() ||
        maximizedId.isInvalid() ||
        minimizedId.isInvalid() ||
        mouseId.isInvalid() ||
        resolutionId.isInvalid() ||
        titleId.isInvalid() ||
        widthId.isInvalid() ||
        mouseXId.isInvalid() ||
        mouseYId.isInvalid() ||
        deltaXId.isInvalid() ||
        deltaYId.isInvalid() ||
        resolutionHeightId.isInvalid() ||
        resolutionWidthId.isInvalid()
    ) {
        raiseError("Failed to register window properties");
    }
}

} // namespace nomad
