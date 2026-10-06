// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/resource/Animation.hpp>
#include <nomad/resource/ResourceManager.hpp>
#include <nomad/resource/Sprite.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

#define VARIABLE_NAME_PREFIX "this"

#define BEGIN_ENTITY_FUNCTION(FUNCTION_NAME) \
    { \
        constexpr const char* ENTITY_FUNCTION_NAME = "this." #FUNCTION_NAME; \
        m_runtime->registerNativeFunction( \
            ENTITY_FUNCTION_NAME, \

#define END_ENTITY_FUNCTION() \
        ); \
    }

#define BEGIN_ENTITY_BLOCK() \
    auto entity = getCurrentContext()->getThisEntity(); \
    if (entity == nullptr) { \
        log::warning(std::format("Cannot call {} without an entity", ENTITY_FUNCTION_NAME));  \
        return; \
    }

#define END_ENTITY_BLOCK()

#define BEGIN_SINGLE_ENTITY_BLOCK() \
    auto entity = getCurrentContext()->getThisEntity(); \
    if (entity != nullptr) { \

#define END_SINGLE_ENTITY_BLOCK(defaultValue) \
    } else { \
        log::warning(NomadString("Cannot call ") + ENTITY_FUNCTION_NAME + " without an entity"); \
        interpreter->setResult(RuntimeValue(defaultValue)); \
    }

void Game::initThisEntityFunctions() {
    log::debug("Initializing this entity functions");

    #include "_EntityFunctions.inl"

    // Custom nativeFunctions
    m_runtime->registerNativeFunction(
        "this.pauseOthers",
        [this](VirtualMachine * /*interpreter*/) {
            auto context = getCurrentContext();

            auto entity = context->getThisEntity();
            auto scene = context->getScene();

            if (entity == nullptr) {
                log::info("{this.pauseOthers}: No entity in current context");

                return;
            }

            if (scene == nullptr) {
                log::info("{this.pauseOthers} No scene in current context");

                return;
            }

            scene->pauseOtherEntities(entity);
        },
        {},
        m_runtime->getVoidType(),
        NomadDoc("Pauses all other entities in the scene except `this`.")
    );
}

} // namespace nomad
