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

#define VARIABLE_NAME_PREFIX "other"

#define BEGIN_ENTITY_FUNCTION(FUNCTION_NAME) \
    { \
        constexpr const char* ENTITY_FUNCTION_NAME = "other." #FUNCTION_NAME; \
        m_runtime->registerNativeFunction( \
           ENTITY_FUNCTION_NAME,

#define END_ENTITY_FUNCTION() \
        ); \
    }

#define BEGIN_ENTITY_BLOCK() \
        getCurrentContext()->forEachOtherEntities([&,this](Entity* entity) {

#define END_ENTITY_BLOCK() \
        });

#define BEGIN_SINGLE_ENTITY_BLOCK() \
        auto currentContext = getCurrentContext(); \
        \
        if (currentContext->getOtherEntityCount()) {\
            currentContext->forEachOtherEntities([&,this](Entity* entity) {

#define END_SINGLE_ENTITY_BLOCK(defaultValue) \
            }); \
        } else { \
            log::warning(NomadString("Cannot call ") + ENTITY_FUNCTION_NAME + " without an entity"); \
            interpreter->setResult(RuntimeValue(defaultValue)); \
        }

void Game::initOtherEntityFunctions() {
    log::debug("Initializing other entity functions");

    #include "_EntityFunctions.inl"

    // Custom nativeFunctions
    m_runtime->registerNativeFunction(
        "other.pauseOthers",
        [this](VirtualMachine * /*interpreter*/) {
            auto context = getCurrentContext();

            auto scene = context->getScene();

            if (scene == nullptr) {
                log::info("{other.pauseOthers} No scene in current context");

                return;
            }

            scene->pauseOtherEntities(context->getOtherEntities());
        },
        {},
        m_runtime->getVoidType(),
        NomadDoc("Pauses all other entities in the scene")
    );
}

} // namespace nomad
