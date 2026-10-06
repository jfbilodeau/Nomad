// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

void Game::initGameDynamicVariables() {
    log::debug("Initializing game dynamic variables");

    m_runtime->registerDynamicVariable(
        "game.clearColor",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            const auto color = value.getIntegerValue();
            const auto rgba = static_cast<Rgba>(color);
            m_clearColor = Color{rgba};
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setIntegerValue(m_clearColor.rgba);
        },
        m_runtime->getIntegerType(),
        NomadDoc("The clear color of the game window.")
    );

    m_runtime->registerDynamicVariable(
        "game.name",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            const NomadString name = value.getStringValue();

            m_name = name;
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setStringValue(m_name);
        },
        m_runtime->getStringType(),
        NomadDoc("The name of the game.")
    );

    m_runtime->registerDynamicVariable(
        "game.organization",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            const auto organization = value.getStringValue();

            m_organization = organization;
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setStringValue(m_organization);
        },
        m_runtime->getStringType(),
        NomadDoc("The organization who made the game.")
    );

    m_runtime->registerDynamicVariable(
        DEBUG_CONSOLE_VISIBLE_VARIABLE,
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            setDebugConsoleVisible(value.getBooleanValue());
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            ensureDebugSettingsLoaded();
            value.setBooleanValue(m_debugConsoleVisible);
        },
        m_runtime->getBooleanType(),
        NomadDoc("Whether the debug console is visible. Only has an effect in debug mode. Saved to `debug.json` when changed.")
    );

    m_runtime->registerDynamicVariable(
        DEBUG_CONSOLE_SCALE_VARIABLE,
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            setDebugConsoleScale(value.getFloatValue());
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            ensureDebugSettingsLoaded();
            value.setFloatValue(m_debugConsoleScale);
        },
        m_runtime->getFloatType(),
        NomadDoc("The debug console UI scale, clamped between 0.8 and 2.0. Saved to `debug.json` when changed.")
    );
}

} // nomad
