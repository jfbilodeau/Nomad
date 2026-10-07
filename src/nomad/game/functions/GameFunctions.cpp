// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/resource/ResourceManager.hpp>

#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Closure.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

void Game::initGameFunctions() {
    log::debug("Initializing game functions");

    const auto bindFunction = [this](const NomadString& name, NativeFunctionFn callback) {
        if (!m_runtime->bindNativeFunction(name, std::move(callback))) {
            throw NomadBug("Failed to bind native function '" + name + "'");
        }
    };

    bindFunction(
        "game.createScene",
        [this](VirtualMachine* interpreter) {
            const auto functionId = interpreter->getIdParameter(0);

            const auto result = m_runtime->getFunctionName(functionId);

            if (!result) {
                log::error("Function ID for scene not found");

                interpreter->setIdResult(NOMAD_INVALID_ID);
            }

            const auto& functionName = result.value();

            const auto sceneId = createScene(functionName, functionId);

            interpreter->setIdResult(sceneId);
        }
    );

    bindFunction(
        "game.createSceneByName",
        [this](VirtualMachine* interpreter) {
            const NomadString functionName = interpreter->getStringParameter(0);

            const auto functionId = getFunctionId(functionName);

            if (functionId == NOMAD_INVALID_ID) {
                log::error("Function for scene '" + functionName + "' not found");
                return;
            }

            const auto sceneId = createScene(functionName, functionId);

            interpreter->setIdResult(sceneId);
        }
    );

    bindFunction(
        "game.createSceneByNameThen",
        [this](VirtualMachine* interpreter) {
            const NomadString functionName = interpreter->getStringParameter(0);
            const NomadId postCreateFunction = interpreter->getIdParameter(1);

            auto postCreateClosure = interpreter->createClosure(m_runtime.get(), postCreateFunction);

            const auto functionId = getFunctionId(functionName);

            if (functionId == NOMAD_INVALID_ID) {
                log::error("Function for scene '" + functionName + "' not found");
                return;
            }

            const auto sceneId = createScene(functionName, functionId, std::move(postCreateClosure));

            interpreter->setIdResult(sceneId);
        }
    );

    bindFunction(
        "game.createSceneThen",
        [this](VirtualMachine* interpreter) {
            const NomadId functionId = interpreter->getIdParameter(0);
            const NomadId postCreateFunction = interpreter->getIdParameter(1);

            auto postCreateClosure = interpreter->createClosure(m_runtime.get(), postCreateFunction);

            const auto result = m_runtime->getFunctionName(functionId);

            if (!result) {
                log::error("Function ID for scene not found");

                interpreter->setIdResult(NOMAD_INVALID_ID);
            }

            const auto& functionName = result.value();

            const auto sceneId = createScene(functionName, functionId, std::move(postCreateClosure));

            interpreter->setIdResult(sceneId);
        }
    );

    bindFunction(
        "game.isInScene",
        [this](VirtualMachine* interpreter) {
            const auto currentContext = getCurrentContext();

            if (currentContext == nullptr) {
                interpreter->setBooleanResult(false);
                return;
            }

            const auto currentScene = currentContext->getScene();

            interpreter->setBooleanResult(currentScene != nullptr);
        }
    );

    bindFunction(
        "game.loadFont",
        [this](VirtualMachine* interpreter) {
            const auto fontName = interpreter->getStringParameter(0);
            const auto fontSize = interpreter->getIntegerParameter(1);

            const auto fontId = m_resourceManager->getFonts()->registerFont(fontName, static_cast<NomadFloat>(fontSize));

            interpreter->setIdResult(fontId);
        }
    );

    bindFunction(
        "game.loadImage",
        [this](VirtualMachine* interpreter) {
            const NomadString textureName = interpreter->getStringParameter(0);

            const auto textureFileName = textureName + ".png";

            const auto textureId = m_resourceManager->getTextures()->registerTexture(textureFileName);

            interpreter->setIdResult(textureId);
        }
    );

    bindFunction(
        "game.loadSpriteAtlas",
        [this](const VirtualMachine* interpreter) {
            const auto atlas_name = interpreter->getStringParameter(0);

            m_resourceManager->loadSpriteAtlas(atlas_name);
        }
    );

    bindFunction(
        "game.inventory.load",
        [this](const VirtualMachine* interpreter) {
            const NomadString saveName = interpreter->getStringParameter(0);

            loadGame(saveName);
        }
    );

    bindFunction(
        "game.inventory.save",
        [this](const VirtualMachine* interpreter) {
            const NomadString saveName = interpreter->getStringParameter(0);

            saveGame(saveName);
        }
    );

    bindFunction(
        "game.inventory.saveExists",
        [this](VirtualMachine* interpreter) {
            const NomadString saveName = interpreter->getStringParameter(0);

            interpreter->setBooleanResult(saveExists(saveName));
        }
    );

    bindFunction(
        "game.settings.load",
        [this](const VirtualMachine* /*interpreter*/) {
            loadSettings();
        }
    );

    bindFunction(
        "game.settings.save",
        [this](const VirtualMachine* /*interpreter*/) {
            saveSettings();
        }
    );

    bindFunction(
        "game.trigger",
        [this](VirtualMachine* interpreter) {
            const auto eventId = interpreter->getIdParameter(0);

            std::vector<RuntimeValue> args;

            const auto event = m_runtime->getEventDefinition(eventId);

            if (!event) {
                log::error(NomadString("Invalid event id for game.trigger: ") + toString(eventId));
                return;
            }

            for (std::size_t i = 0; i < event->parameters.size(); ++i) {
                const auto& parameter = event->parameters[i];

                const auto type = m_runtime->getType(parameter.typeId);

                RuntimeValue value;

                if (type) {
                    (*type)->copyValue(interpreter->peekStack(static_cast<NomadIndex>(i) + 1), value);
                } else {
                    log::error("Invalid type id for event parameter: " + toString(parameter.typeId));
                }

                args.emplace_back(value);
            }

            dispatchEvent(EventDispatch {
                eventId,
                NOMAD_INVALID_ID,
                NOMAD_INVALID_ID,
                NOMAD_INVALID_ID,
                0,
                std::move(args),
            });
        }
    );
}

} // namespace nomad
