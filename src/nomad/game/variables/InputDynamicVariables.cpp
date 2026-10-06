// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

void Game::initInputDynamicVariables() {
    log::debug("Initializing input dynamic variables");

    m_runtime->registerDynamicVariable(
        "input.mouse.x",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            m_mousePosition.setX(value.getFloatValue());
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setFloatValue(m_mousePosition.getX());
        },
        m_runtime->getFloatType(),
        NomadDoc("The x position of the mouse.")
    );

    m_runtime->registerDynamicVariable(
        "input.mouse.y",
        [this](VirtualMachine* /*interpreter*/, const RuntimeValue& value) {
            m_mousePosition.setY(value.getFloatValue());
        },
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setFloatValue(m_mousePosition.getY());
        },
        m_runtime->getFloatType(),
        NomadDoc("The y position of the mouse.")
    );

    m_runtime->registerDynamicVariable(
        "input.mouse.deltaX",
        nullptr,
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setFloatValue(m_mouseLastPosition.getX() - m_mousePosition.getX());
        },
        m_runtime->getFloatType(),
        NomadDoc("The change in x position of the mouse since last frame.")
    );

    m_runtime->registerDynamicVariable(
        "input.mouse.deltaY",
        nullptr,
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            value.setFloatValue(m_mouseLastPosition.getY() - m_mousePosition.getY());
        },
        m_runtime->getFloatType(),
        NomadDoc("The change in y position of the mouse since last frame.")
    );
}

} // nomad
