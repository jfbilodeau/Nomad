// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/game/resource/FontObject.hpp>
#include <nomad/game/resource/ResourceManager.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

void Game::initGameFunctions() {
    log::debug("Initializing game functions");

    const auto hubId = m_runtime->getHubId("game");
    const auto* fontType = m_runtime->getTypeByName("Font");
    if (fontType == nullptr) {
        raiseError("Font type is not registered");
    }

    const auto createSceneId = m_runtime->registerHubNativeFunction(
        hubId,
        "createScene",
        [this](VirtualMachine* interpreter) {
            const auto callableId = interpreter->getCallableIdParameter(0);

            const auto result = m_runtime->getScriptFunctionName(callableId);

            if (!result) {
                log::error("Function ID for scene not found");

                interpreter->setIdResult(NOMAD_INVALID_ID);
            }

            const auto& functionName = result.value();

            const auto sceneId = createScene(functionName, callableId);

            interpreter->setIdResult(sceneId);
        }, {
            defParameter(
                "function",
                m_runtime->getTypeManager().getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("The function to execute to initialize the scene. Return the scene id.")
            )
        },
        m_runtime->getIntegerType(),
        NomadDoc("Creates a new scene.")
    );

    const auto createSceneByNameId = m_runtime->registerHubNativeFunction(
        hubId,
        "createSceneByName",
        [this](VirtualMachine* interpreter) {
            const auto& functionName = interpreter->getStringParameter(0)->getString();

            const auto functionId = getFunctionId(functionName);

            if (functionId.isInvalid()) {
                log::error("Function for scene '" + functionName + "' not found");
                return;
            }

            const auto sceneId = createScene(functionName, functionId);

            interpreter->setIdResult(sceneId);
        }, {
            defParameter(
                "functionName", m_runtime->getStringType(),
                NomadParamDoc("Name of the function to execute to initialize the scene. Return the scene id.")
            )
        },
        m_runtime->getIntegerType(),
        NomadDoc("Creates a new scene.")
    );

    const auto createSceneByNameThenId = m_runtime->registerHubNativeFunction(
        hubId,
        "createSceneByNameThen",
        [this](VirtualMachine* interpreter) {
            const auto& functionName = interpreter->getStringParameter(0)->getString();
            const auto postCreateFunction = interpreter->getCallableIdParameter(1);

            auto postCreateClosure = interpreter->createClosure(m_runtime, postCreateFunction);

            const auto functionId = getFunctionId(functionName);

            if (functionId.isInvalid()) {
                log::error("Function for scene '" + functionName + "' not found");
                return;
            }

            const auto sceneId = createScene(functionName, functionId, std::move(postCreateClosure));

            interpreter->setIdResult(sceneId);
        },
        {
            defParameter(
                "functionName",
                m_runtime->getStringType(),
                NomadParamDoc("Name of the function to execute to initialize the scene.")
            ),
            defParameter(
                "postCreateFunction",
                m_runtime->getTypeManager().getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("Function to execute once the scene is created.")
            )
        },
        m_runtime->getIntegerType(),
        NomadDoc("Creates a new scene and execute a function once the scene is created. Returns the scene id.")
    );

    const auto createSceneThenId = m_runtime->registerHubNativeFunction(
        hubId,
        "createSceneThen",
        [this](VirtualMachine* interpreter) {
            const auto functionId = interpreter->getCallableIdParameter(0);
            const auto postCreateFunction = interpreter->getCallableIdParameter(1);

            auto postCreateClosure = interpreter->createClosure(m_runtime, postCreateFunction);

            const auto result = m_runtime->getScriptFunctionName(functionId);

            if (!result) {
                log::error("Function ID for scene not found");

                interpreter->setIdResult(NOMAD_INVALID_ID);
            }

            const auto& functionName = result.value();

            const auto sceneId = createScene(functionName, functionId, std::move(postCreateClosure));

            interpreter->setIdResult(sceneId);
        },
        {
            defParameter(
                "function", m_runtime->getTypeManager().getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("The function to execute to initialize the scene.")
            ),
            defParameter(
                "postCreateFunction", m_runtime->getTypeManager().getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("Function to execute once the scene is created.")
            )
        },
        m_runtime->getIntegerType(),
        NomadDoc("Creates a new scene and execute a function once the scene is created. Returns the scene id.")
    );

    const auto isInSceneId = m_runtime->registerHubNativeFunction(
        hubId,
        "isInScene",
        [this](VirtualMachine* interpreter) {

            const auto currentContext = getCurrentContext();

            if (currentContext == nullptr) {
                interpreter->setBooleanResult(false);
                return;
            }

            const auto currentScene = currentContext->getScene();

            interpreter->setBooleanResult(currentScene != nullptr);
        },
        { },
        m_runtime->getBooleanType(),
        NomadDoc("Returns true if this function is executing in a scene.")
    );

    const auto loadFontId = m_runtime->registerHubNativeFunction(
        hubId,
        "loadFont",
        [this](VirtualMachine* interpreter) {
            const auto& fontName = interpreter->getStringParameter(0)->getString();
            const auto fontSize = interpreter->getIntegerParameter(1);

            auto* font = m_resourceManager->getFonts()->registerFont(fontName, static_cast<NomadFloat>(fontSize));
            interpreter->getResult().setObjectValue(font);
        }, {
            defParameter("fontName", m_runtime->getStringType(), NomadParamDoc("Name of the font to load.")),
            defParameter("fontSize", m_runtime->getIntegerType(), NomadParamDoc("Size of the font in point to load.")),
        },
        fontType,
        NomadDoc("Loads a font from a file and returns a Font object.")
    );

    const auto loadImageId = m_runtime->registerHubNativeFunction(
        hubId,
        "loadImage",
        [this](VirtualMachine* interpreter) {
            const auto& textureName = interpreter->getStringParameter(0)->getString();

            const auto textureFileName = textureName + ".png";

            const auto textureId = m_resourceManager->getTextures()->registerTexture(textureFileName);

            interpreter->setIdResult(textureId);
        }, {
            defParameter("imageName", m_runtime->getStringType(), NomadParamDoc("Name of the image to load.")),
        },
        m_runtime->getIntegerType(),
        NomadDoc("Loads a font from a file.")
    );

    const auto triggerId = m_runtime->registerHubNativeFunction(
        hubId,
        "trigger",
        [this](VirtualMachine* interpreter) {
            const auto eventId = EventId{interpreter->getIdParameter(0)};

            std::vector<RuntimeValue> args;

            const auto* event = m_runtime->getEventDefinition(eventId);

            if (!event) {
                log::error(NomadString("Invalid event id for game.trigger: ") + toString(eventId.id));
                return;
            }

            for (std::size_t i = 0; i < event->parameters.size(); ++i) {
                const auto& parameter = event->parameters[i];

                const auto type = m_runtime->getType(parameter.typeId);

                RuntimeValue value;

                if (type) {
                    (*type)->copyValue(&interpreter->peekStack(static_cast<NomadIndex>(i) + 1), &value);
                } else {
                    log::error("Invalid type id for event parameter: " + toString(parameter.typeId.id));
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
        }, {
            defParameter("event", m_runtime->getEventDispatchType(), NomadParamDoc("The event to dispatch (trigger).")),
        },
        m_runtime->getVoidType(),
        NomadDoc("Trigger a global event.")
    );

    if (
        createSceneId.isInvalid() ||
        createSceneByNameId.isInvalid() ||
        createSceneByNameThenId.isInvalid() ||
        createSceneThenId.isInvalid() ||
        isInSceneId.isInvalid() ||
        loadFontId.isInvalid() ||
        loadImageId.isInvalid() ||
        triggerId.isInvalid()
    ) {
        raiseError("Failed to register game hub functions");
    }

}

} // namespace nomad
