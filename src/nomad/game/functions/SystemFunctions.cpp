// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Color.hpp>

#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

void Game::initSystemFunctions() {
    log::debug("Initializing system functions");

    if (!m_runtime->bindNativeFunction(
        "rgb",
        [this](VirtualMachine* interpreter) {
            const auto r = static_cast<NomadInteger>(interpreter->getIntegerParameter(0));
            const auto g = static_cast<NomadInteger>(interpreter->getIntegerParameter(1));
            const auto b = static_cast<NomadInteger>(interpreter->getIntegerParameter(2));

            Color color{
                static_cast<Uint8>(r),
                static_cast<Uint8>(g),
                static_cast<Uint8>(b),
                255
            };

            interpreter->setIntegerResult(color.rgba);
        }
    )) {
        throw NomadBug("Failed to bind native function 'rgb'");
    }

    if (!m_runtime->bindNativeFunction(
        "rgba",
        [this](VirtualMachine* interpreter) {
            const auto r = static_cast<NomadInteger>(interpreter->getIntegerParameter(0));
            const auto g = static_cast<NomadInteger>(interpreter->getIntegerParameter(1));
            const auto b = static_cast<NomadInteger>(interpreter->getIntegerParameter(2));
            const auto a = static_cast<NomadInteger>(interpreter->getIntegerParameter(3));

            const NomadInteger color = (r << 24) | (g << 16) | (b << 8) | a;

            interpreter->setIntegerResult(color);
        }
    )) {
        throw NomadBug("Failed to bind native function 'rgba'");
    }

    if (!m_runtime->bindNativeFunction(
        "system.exit",
        [this](VirtualMachine* /*interpreter*/) {
            quit();
        }
    )) {
        throw NomadBug("Failed to bind native function 'system.exit'");
    }
}

} // nomad
