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
    const auto registerWindowCallback = [this](
        const NomadString& name,
        const NomadString& clearName,
        std::shared_ptr<Closure> Game::* callback,
        const std::vector<const Type*>& parameterTypes,
        const NomadString& documentation
    ) {
        m_runtime->registerNativeFunction(
            name,
            [this, callback](VirtualMachine* interpreter) {
                const auto functionId = interpreter->getIdParameter(0);
                this->*callback = interpreter->createClosure(m_runtime.get(), functionId);
            }, {
                defParameter(
                    "callback",
                    m_runtime->getCallbackType(parameterTypes, m_runtime->getVoidType()),
                    NomadParamDoc("Callback to invoke when the window event occurs.")
                )
            },
            m_runtime->getVoidType(),
            NomadDoc(documentation)
        );

        m_runtime->registerNativeFunction(
            clearName,
            [this, callback](VirtualMachine* /*interpreter*/) {
                (this->*callback).reset();
            },
            {},
            m_runtime->getVoidType(),
            NomadDoc("Clears the callback registered by " + name + ".")
        );
    };

    registerWindowCallback(
        "window.onClose",
        "window.clearOnClose",
        &Game::m_onWindowClose,
        {},
        "Sets the callback invoked when the user requests that the window close. The window is not closed automatically."
    );

    registerWindowCallback(
        "window.onGainFocus",
        "window.clearOnGainFocus",
        &Game::m_onWindowGainFocus,
        {},
        "Sets the callback invoked when the window gains focus."
    );

    registerWindowCallback(
        "window.onLoseFocus",
        "window.clearOnLoseFocus",
        &Game::m_onWindowLoseFocus,
        {},
        "Sets the callback invoked when the window loses focus."
    );

    registerWindowCallback(
        "window.onMaximize",
        "window.clearOnMaximize",
        &Game::m_onWindowMaximize,
        {},
        "Sets the callback invoked when the window is maximized."
    );

    registerWindowCallback(
        "window.onMinimize",
        "window.clearOnMinimize",
        &Game::m_onWindowMinimize,
        {},
        "Sets the callback invoked when the window is minimized."
    );

    registerWindowCallback(
        "window.onMove",
        "window.clearOnMove",
        &Game::m_onWindowMove,
        {m_runtime->getIntegerType(), m_runtime->getIntegerType()},
        "Sets the callback invoked when the window is moved. Receives the x and y position."
    );

    registerWindowCallback(
        "window.onResize",
        "window.clearOnResize",
        &Game::m_onWindowResize,
        {m_runtime->getIntegerType(), m_runtime->getIntegerType()},
        "Sets the callback invoked when the window is resized. Receives the width and height."
    );

    registerWindowCallback(
        "window.onRestore",
        "window.clearOnRestore",
        &Game::m_onWindowRestore,
        {},
        "Sets the callback invoked when the window is restored."
    );
}

} // nomad
