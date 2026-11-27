// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/WindowObject.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

void Game::initWindowFunctions() {
    log::debug("Initializing window methods");

    const auto typeId = m_runtime->getTypeId("game.Window");
    const auto voidTypeId = m_runtime->getVoidType()->getId();
    const auto integerTypeId = m_runtime->getIntegerType()->getId();

    const auto maximizeId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "maximize",
        .parameters = {},
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& game = machine->getThisAs<WindowObject>()->getGame();
            if (!SDL_MaximizeWindow(game.m_window)) {
                log::error(NomadString("Unable to maximize window: ") + SDL_GetError());
            }
        },
        .documentation = NomadDoc("Maximizes the window.")
    });

    const auto minimizeId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "minimize",
        .parameters = {},
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& game = machine->getThisAs<WindowObject>()->getGame();
            if (!SDL_MinimizeWindow(game.m_window)) {
                log::error(NomadString("Unable to minimize window: ") + SDL_GetError());
            }
        },
        .documentation = NomadDoc("Minimizes the window.")
    });

    const auto fpsId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "setFps",
        .parameters = {{integerTypeId, "framesPerSecond", NomadParamDoc("Frames per second.")}},
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& game = machine->getThisAs<WindowObject>()->getGame();
            game.m_fps = machine->getIntegerParameter(0);
        },
        .documentation = NomadDoc("Sets the frames per second (FPS) of the game.")
    });

    const auto resolutionId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "setResolution",
        .parameters = {
            {integerTypeId, "resolutionWidth", NomadParamDoc("Resolution width.")},
            {integerTypeId, "resolutionHeight", NomadParamDoc("Resolution height.")}
        },
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& game = machine->getThisAs<WindowObject>()->getGame();
            game.setResolution(
                static_cast<int>(machine->getIntegerParameter(0)),
                static_cast<int>(machine->getIntegerParameter(1))
            );
        },
        .documentation = NomadDoc("Sets the resolution of the game window.")
    });

    const auto sizeId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "setSize",
        .parameters = {
            {integerTypeId, "windowWidth", NomadParamDoc("Window width.")},
            {integerTypeId, "windowHeight", NomadParamDoc("Window height.")}
        },
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& game = machine->getThisAs<WindowObject>()->getGame();
            game.setWindowSize(
                static_cast<int>(machine->getIntegerParameter(0)),
                static_cast<int>(machine->getIntegerParameter(1))
            );
        },
        .documentation = NomadDoc("Sets the size of the game window.")
    });

    const auto sizeAndCenterId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "setSizeAndCenter",
        .parameters = {
            {integerTypeId, "windowWidth", NomadParamDoc("Window width.")},
            {integerTypeId, "windowHeight", NomadParamDoc("Window height.")}
        },
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& game = machine->getThisAs<WindowObject>()->getGame();
            game.setWindowSize(
                static_cast<int>(machine->getIntegerParameter(0)),
                static_cast<int>(machine->getIntegerParameter(1))
            );
            game.centerWindow();
        },
        .documentation = NomadDoc("Sets the size of the game window and centers it.")
    });

    const auto titleId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "setTitle",
        .parameters = {{
            m_runtime->getStringType()->getId(), "windowTitle", NomadParamDoc("Title of the game window.")
        }},
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& game = machine->getThisAs<WindowObject>()->getGame();
            game.setWindowTitle(machine->getStringParameter(0)->getString());
        },
        .documentation = NomadDoc("Sets the title of the game window.")
    });

    const auto fullscreenId = m_runtime->registerNativeClassMethod(typeId, {
        .name = "toggleFullScreen",
        .parameters = {},
        .returnTypeId = voidTypeId,
        .function = [](VirtualMachine* machine) {
            auto& game = machine->getThisAs<WindowObject>()->getGame();
            const auto fullscreen = (SDL_GetWindowFlags(game.m_window) & SDL_WINDOW_FULLSCREEN) != 0;
            if (!SDL_SetWindowFullscreen(game.m_window, !fullscreen)) {
                log::error(NomadString("Unable to toggle window full-screen mode: ") + SDL_GetError());
            }
        },
        .documentation = NomadDoc("Toggles the window between windowed and full-screen modes.")
    });

    if (
        maximizeId.isInvalid() ||
        minimizeId.isInvalid() ||
        fpsId.isInvalid() ||
        resolutionId.isInvalid() ||
        sizeId.isInvalid() ||
        sizeAndCenterId.isInvalid() ||
        titleId.isInvalid() ||
        fullscreenId.isInvalid()
    ) {
        raiseError("Failed to register window methods");
    }
}

void Game::initWindowCallbacks() {
    const auto typeId = m_runtime->getTypeId("game.Window");
    const auto registerWindowCallback = [this, typeId](
        const NomadString& name,
        const NomadString& clearName,
        std::shared_ptr<Closure> Game::* callback,
        const std::vector<const Type*>& parameterTypes,
        const NomadString& documentation
    ) {
        const auto callbackTypeId = m_runtime->getTypeManager().getCallbackType(
            parameterTypes, m_runtime->getVoidType()
        )->getId();
        const auto registerId = m_runtime->registerNativeClassMethod(typeId, {
            .name = name,
            .parameters = {{
                callbackTypeId, "callback", NomadParamDoc("Callback to invoke when the window event occurs.")
            }},
            .returnTypeId = m_runtime->getVoidType()->getId(),
            .function = [callback](VirtualMachine* machine) {
                auto& game = machine->getThisAs<WindowObject>()->getGame();
                const auto callableId = machine->getCallableIdParameter(0);
                game.*callback = machine->createClosure(game.m_runtime, callableId);
            },
            .documentation = NomadDoc(documentation)
        });

        const auto clearId = m_runtime->registerNativeClassMethod(typeId, {
            .name = clearName,
            .parameters = {},
            .returnTypeId = m_runtime->getVoidType()->getId(),
            .function = [callback](VirtualMachine* machine) {
                auto& game = machine->getThisAs<WindowObject>()->getGame();
                (game.*callback).reset();
            },
            .documentation = NomadDoc("Clears the callback registered by game.window." + name + ".")
        });

        if (registerId.isInvalid() || clearId.isInvalid()) {
            raiseError("Failed to register window callback '" + name + "'");
        }
    };

    registerWindowCallback(
        "onClose",
        "clearOnClose",
        &Game::m_onWindowClose,
        {},
        "Sets the callback invoked when the user requests that the window close. The window is not closed automatically."
    );

    registerWindowCallback(
        "onGainFocus",
        "clearOnGainFocus",
        &Game::m_onWindowGainFocus,
        {},
        "Sets the callback invoked when the window gains focus."
    );

    registerWindowCallback(
        "onLoseFocus",
        "clearOnLoseFocus",
        &Game::m_onWindowLoseFocus,
        {},
        "Sets the callback invoked when the window loses focus."
    );

    registerWindowCallback(
        "onMaximize",
        "clearOnMaximize",
        &Game::m_onWindowMaximize,
        {},
        "Sets the callback invoked when the window is maximized."
    );

    registerWindowCallback(
        "onMinimize",
        "clearOnMinimize",
        &Game::m_onWindowMinimize,
        {},
        "Sets the callback invoked when the window is minimized."
    );

    registerWindowCallback(
        "onMove",
        "clearOnMove",
        &Game::m_onWindowMove,
        {m_runtime->getIntegerType(), m_runtime->getIntegerType()},
        "Sets the callback invoked when the window is moved. Receives the x and y position."
    );

    registerWindowCallback(
        "onResize",
        "clearOnResize",
        &Game::m_onWindowResize,
        {m_runtime->getIntegerType(), m_runtime->getIntegerType()},
        "Sets the callback invoked when the window is resized. Receives the width and height."
    );

    registerWindowCallback(
        "onRestore",
        "clearOnRestore",
        &Game::m_onWindowRestore,
        {},
        "Sets the callback invoked when the window is restored."
    );
}

} // nomad
