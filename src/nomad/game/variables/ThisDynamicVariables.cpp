// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>
#include <nomad/resource/Sprite.hpp>

#include <nomad/script/VirtualMachine.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

#define VARIABLE_NAME_PREFIX "this"

#define BEGIN_ENTITY_BLOCK(message) \
    auto entity = getCurrentContext()->getThisEntity(); \
    if (entity == nullptr) { \
        log::error(message);  \
        return; \
    }

#define END_ENTITY_BLOCK

#define BEGIN_SINGLE_ENTITY_BLOCK(message) \
    BEGIN_ENTITY_BLOCK(message)

#define END_SINGLE_ENTITY_BLOCK(default_value)

void Game::initThisDynamicVariables() {
    log::debug("Initializing this dynamic variables");

    m_runtime->registerDynamicVariable(
        "this",
        nullptr,
        [this](VirtualMachine* /*interpreter*/, RuntimeValue& value) {
            BEGIN_ENTITY_BLOCK("Cannot access 'this' outside of an entity")

            value.setIntegerValue(entity->getId());

            END_SINGLE_ENTITY_BLOCK(NOMAD_INVALID_ID)
        },
        m_runtime->getIntegerType(),
        "Get the ID of the `this` entity."
    );

    #include "_EntityDynamicVariables.inl"
}

} // namespace nomad
