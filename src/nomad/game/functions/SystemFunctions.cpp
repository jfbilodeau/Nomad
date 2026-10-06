// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Color.hpp>

#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

void Game::initSystemFunctions() {
    log::debug("Initializing system functions");

    m_runtime->registerNativeFunction(
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
        },
        {
            defParameter("r", m_runtime->getIntegerType(), NomadParamDoc("Red component (0-255).")),
            defParameter("g", m_runtime->getIntegerType(), NomadParamDoc("Green component (0-255).")),
            defParameter("b", m_runtime->getIntegerType(), NomadParamDoc("Blue component (0-255)."))
        },
        m_runtime->getIntegerType(),
        NomadDoc("Creates an RGB color from red, green and blue components (0-255).")
    );

    m_runtime->registerNativeFunction(
        "rgba",
        [this](VirtualMachine* interpreter) {
            const auto r = static_cast<NomadInteger>(interpreter->getIntegerParameter(0));
            const auto g = static_cast<NomadInteger>(interpreter->getIntegerParameter(1));
            const auto b = static_cast<NomadInteger>(interpreter->getIntegerParameter(2));
            const auto a = static_cast<NomadInteger>(interpreter->getIntegerParameter(3));

            const NomadInteger color = (r << 24) | (g << 16) | (b << 8) | a;

            interpreter->setIntegerResult(color);
        },
        {
            defParameter("r", m_runtime->getIntegerType(), NomadParamDoc("Red component (0-255).")),
            defParameter("g", m_runtime->getIntegerType(), NomadParamDoc("Green component (0-255).")),
            defParameter("b", m_runtime->getIntegerType(), NomadParamDoc("Blue component (0-255).")),
            defParameter("a", m_runtime->getIntegerType(), NomadParamDoc("Alpha component (0-255)."))
        },
        m_runtime->getIntegerType(),
        NomadDoc("Creates an RGBA color from red, green, blue and alpha components (0-255).")
    );

    m_runtime->registerNativeFunction(
        "system.exit",
        [this](VirtualMachine* /*interpreter*/) {
            quit();
        },
        {},
        m_runtime->getVoidType(),
        NomadDoc("Exits the game.")
    );
}

} // nomad
