// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/resource/ResourceManager.hpp>

#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Closure.hpp>
#include <nomad/script/Runtime.hpp>

#include <utility>

namespace nomad {

void Game::initWindowFunctions() {
    log::debug("Initializing window functions");

    const auto bindFunction = [this](const NomadString& name, NativeFunctionFn callback) {
        if (!m_runtime->bindNativeFunction(name, std::move(callback))) {
            throw NomadBug("Failed to bind native function '" + name + "'");
        }
    };

    bindFunction(
        "window.maximize",
        [this](VirtualMachine* /*interpreter*/) {
            if (!SDL_MaximizeWindow(m_window)) {
                log::error(NomadString("Unable to maximize window: ") + SDL_GetError());
            }
        }
    );

    bindFunction(
        "window.minimize",
        [this](VirtualMachine* /*interpreter*/) {
            if (!SDL_MinimizeWindow(m_window)) {
                log::error(NomadString("Unable to minimize window: ") + SDL_GetError());
            }
        }
    );

    bindFunction(
        "window.setFps",
        [this](const VirtualMachine* interpreter) {
            const auto fps = static_cast<int>(interpreter->getIntegerParameter(0));

            m_fps = fps;
        }
    );

    bindFunction(
        "window.setResolution",
        [this](VirtualMachine* interpreter) {
            const auto resolutionX = static_cast<int>(interpreter->getIntegerParameter(0));
            const auto resolutionY = static_cast<int>(interpreter->getIntegerParameter(1));

            setResolution(resolutionX, resolutionY);
        }
    );

    bindFunction(
        "window.setSize",
        [this](const VirtualMachine* interpreter) {
            const auto width = static_cast<int>(interpreter->getIntegerParameter(0));
            const auto height = static_cast<int>(interpreter->getIntegerParameter(1));

            setWindowSize(width, height);
        }
    );

    bindFunction(
        "window.setSizeAndCenter",
        [this](const VirtualMachine* interpreter) {
            const auto width = static_cast<int>(interpreter->getIntegerParameter(0));
            const auto height = static_cast<int>(interpreter->getIntegerParameter(1));

            setWindowSize(width, height);

            centerWindow();
        }
    );

    bindFunction(
        "window.setTitle",
        [this](const VirtualMachine* interpreter) {
            const auto title = interpreter->getStringParameter(0);

            SDL_SetWindowTitle(m_window, title);
        }
    );

    bindFunction(
        "window.toggleFullScreen",
        [this](VirtualMachine* /*interpreter*/) {
            const auto fullscreen = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
            if (!SDL_SetWindowFullscreen(m_window, !fullscreen)) {
                log::error(NomadString("Unable to toggle window full-screen mode: ") + SDL_GetError());
            }
        }
    );
}

void Game::initWindowCallbacks() {
    const auto bindWindowCallback = [this](
        const NomadString& name,
        const NomadString& clearName,
        std::shared_ptr<Closure> Game::* callback
    ) {
        if (!m_runtime->bindNativeFunction(
            name,
            [this, callback](VirtualMachine* interpreter) {
                const auto functionId = interpreter->getIdParameter(0);
                this->*callback = interpreter->createClosure(m_runtime.get(), functionId);
            }
        )) {
            throw NomadBug("Failed to bind native function '" + name + "'");
        }

        if (!m_runtime->bindNativeFunction(
            clearName,
            [this, callback](VirtualMachine* /*interpreter*/) {
                (this->*callback).reset();
            }
        )) {
            throw NomadBug("Failed to bind native function '" + clearName + "'");
        }
    };

    bindWindowCallback(
        "window.onClose",
        "window.clearOnClose",
        &Game::m_onWindowClose
    );

    bindWindowCallback(
        "window.onGainFocus",
        "window.clearOnGainFocus",
        &Game::m_onWindowGainFocus
    );

    bindWindowCallback(
        "window.onLoseFocus",
        "window.clearOnLoseFocus",
        &Game::m_onWindowLoseFocus
    );

    bindWindowCallback(
        "window.onMaximize",
        "window.clearOnMaximize",
        &Game::m_onWindowMaximize
    );

    bindWindowCallback(
        "window.onMinimize",
        "window.clearOnMinimize",
        &Game::m_onWindowMinimize
    );

    bindWindowCallback(
        "window.onMove",
        "window.clearOnMove",
        &Game::m_onWindowMove
    );

    bindWindowCallback(
        "window.onResize",
        "window.clearOnResize",
        &Game::m_onWindowResize
    );

    bindWindowCallback(
        "window.onRestore",
        "window.clearOnRestore",
        &Game::m_onWindowRestore
    );
}

} // nomad
