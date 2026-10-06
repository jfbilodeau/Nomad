// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

void Game::initWindowDynamicVariables() {
    log::debug("Initializing window dynamic variables");

    m_runtime->registerDynamicVariable(
        "window.fps",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            m_fps = int(value.getIntegerValue());
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setIntegerValue(m_fps);
        },
        getRuntime()->getIntegerType(),
        NomadDoc("The frames per second (FPS) of the game.")
    );

    m_runtime->registerDynamicVariable(
        "window.hasFocus",
        {},
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setBooleanValue((SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS) != 0);
        },
        getRuntime()->getBooleanType(),
        NomadDoc("Whether the game window currently has input focus.")
    );

    m_runtime->registerDynamicVariable(
        "window.height",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            m_windowSize.setY(int(value.getIntegerValue()));

            SDL_SetWindowSize(
                m_window,
                static_cast<int>(m_windowSize.getX()),
                static_cast<int>(m_windowSize.getY())
            );
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setIntegerValue(m_windowSize.getY());
        },
        getRuntime()->getIntegerType(),
        NomadDoc("The height of the game window.")
    );

    m_runtime->registerDynamicVariable(
        "window.isFullScreen",
        {},
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setBooleanValue((SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0);
        },
        getRuntime()->getBooleanType(),
        NomadDoc("Whether the game window is currently in full-screen mode.")
    );

    m_runtime->registerDynamicVariable(
        "window.isMaximized",
        {},
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setBooleanValue((SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) != 0);
        },
        getRuntime()->getBooleanType(),
        NomadDoc("Whether the game window is currently maximized.")
    );

    m_runtime->registerDynamicVariable(
        "window.isMinimized",
        {},
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setBooleanValue((SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0);
        },
        getRuntime()->getBooleanType(),
        NomadDoc("Whether the game window is currently minimized.")
    );

    m_runtime->registerDynamicVariable(
        "window.resolution.height",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            const auto width = getResolution().getX();
            const auto height = static_cast<int>(value.getIntegerValue());

            setResolution(width, height);
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setIntegerValue(m_resolution.getY());
        },
        getRuntime()->getIntegerType(),
        NomadDoc("The vertical resolution of the game.")
    );

    m_runtime->registerDynamicVariable(
        "window.resolution.width",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            const auto width = static_cast<int>(value.getIntegerValue());
            const auto height = getResolution().getY();

            setResolution(width, height);
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setIntegerValue(m_resolution.getX());
        },
        getRuntime()->getIntegerType(),
        NomadDoc("The horizontal resolution of the game.")
    );

    m_runtime->registerDynamicVariable(
        "window.title",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            const auto title = value.getStringValue();

            SDL_SetWindowTitle(m_window, title);
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            const auto title = SDL_GetWindowTitle(m_window);

            value.setStringValue(title);
        },
        getRuntime()->getStringType(),
        NomadDoc("Set the title of the game window.")
    );

    m_runtime->registerDynamicVariable(
        "window.width",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            m_windowSize.setX(int(value.getIntegerValue()));

            SDL_SetWindowSize(
                m_window,
                static_cast<int>(m_windowSize.getX()),
                static_cast<int>(m_windowSize.getY())
            );
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setIntegerValue(m_windowSize.getX());
        },
        getRuntime()->getIntegerType(),
        NomadDoc("The width of the game window.")
    );


}

} // nomad
