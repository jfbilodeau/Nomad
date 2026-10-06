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

    m_runtime->registerNativeFunction(
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
        }, {
            defParameter(
                "function",
                m_runtime->getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("The function to execute to initialize the scene. Return the scene id.")
            )
        },
        m_runtime->getIntegerType(),
        NomadDoc("Creates a new scene.")
    );

    m_runtime->registerNativeFunction(
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
        }, {
            defParameter(
                "functionName", m_runtime->getStringRefType(),
                NomadParamDoc("Name of the function to execute to initialize the scene. Return the scene id.")
            )
        },
        m_runtime->getIntegerType(),
        NomadDoc("Creates a new scene.")
    );

    m_runtime->registerNativeFunction(
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
        },
        {
            defParameter(
                "functionName",
                m_runtime->getStringRefType(),
                NomadParamDoc("Name of the function to execute to initialize the scene.")
            ),
            defParameter(
                "postCreateFunction",
                m_runtime->getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("Function to execute once the scene is created.")
            )
        },
        m_runtime->getIntegerType(),
        NomadDoc("Creates a new scene and execute a function once the scene is created. Returns the scene id.")
    );

    m_runtime->registerNativeFunction(
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
        },
        {
            defParameter(
                "function", m_runtime->getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("The function to execute to initialize the scene.")
            ),
            defParameter(
                "postCreateFunction", m_runtime->getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("Function to execute once the scene is created.")
            )
        },
        m_runtime->getIntegerType(),
        NomadDoc("Creates a new scene and execute a function once the scene is created. Returns the scene id.")
    );

    m_runtime->registerNativeFunction(
        "game.isInScene",
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

    m_runtime->registerNativeFunction(
        "game.loadFont",
        [this](VirtualMachine* interpreter) {
            const auto fontName = interpreter->getStringParameter(0);
            const auto fontSize = interpreter->getIntegerParameter(1);

            const auto fontId = m_resourceManager->getFonts()->registerFont(fontName, static_cast<NomadFloat>(fontSize));

            interpreter->setIdResult(fontId);
        }, {
            defParameter("fontName", m_runtime->getStringRefType(), NomadParamDoc("Name of the font to load.")),
            defParameter("fontSize", m_runtime->getIntegerType(), NomadParamDoc("Size of the font in point to load.")),
        },
        m_runtime->getIntegerType(),
        NomadDoc("Loads a font from a file. Returns the ID of the font.")
    );

    m_runtime->registerNativeFunction(
        "game.loadImage",
        [this](VirtualMachine* interpreter) {
            const NomadString textureName = interpreter->getStringParameter(0);

            const auto textureFileName = textureName + ".png";

            const auto textureId = m_resourceManager->getTextures()->registerTexture(textureFileName);

            interpreter->setIdResult(textureId);
        }, {
            defParameter("imageName", m_runtime->getStringRefType(), NomadParamDoc("Name of the image to load.")),
        },
        m_runtime->getIntegerType(),
        NomadDoc("Loads a font from a file.")
    );

    m_runtime->registerNativeFunction(
        "game.loadSpriteAtlas",
        [this](const VirtualMachine* interpreter) {
            const auto atlas_name = interpreter->getStringParameter(0);

            m_resourceManager->loadSpriteAtlas(atlas_name);
        }, {
            defParameter(
                "atlasName", m_runtime->getStringRefType(), NomadParamDoc("Name of the sprite atlas to load.")
            ),
        },
        m_runtime->getVoidType(),
        NomadDoc("Loads a sprite atlas from a file.")
    );

    m_runtime->registerNativeFunction(
        "game.inventory.load",
        [this](const VirtualMachine* interpreter) {
            const NomadString saveName = interpreter->getStringParameter(0);

            loadGame(saveName);
        }, {
            defParameter("saveName", m_runtime->getStringRefType(), NomadParamDoc("Name of the save to load.")),
        },
        m_runtime->getVoidType(),
        NomadDoc(
            "Loads `inventory.*` variables from `<pref>/save/<saveName>.json`. Variables absent from the save keep "
            "their current value."
        )
    );

    m_runtime->registerNativeFunction(
        "game.inventory.save",
        [this](const VirtualMachine* interpreter) {
            const NomadString saveName = interpreter->getStringParameter(0);

            saveGame(saveName);
        }, {
            defParameter("saveName", m_runtime->getStringRefType(), NomadParamDoc("Name of the save.")),
        },
        m_runtime->getVoidType(),
        NomadDoc("Saves all `inventory.*` variables to `<pref>/save/<saveName>.json`.")
    );

    m_runtime->registerNativeFunction(
        "game.inventory.saveExists",
        [this](VirtualMachine* interpreter) {
            const NomadString saveName = interpreter->getStringParameter(0);

            interpreter->setBooleanResult(saveExists(saveName));
        }, {
            defParameter("saveName", m_runtime->getStringRefType(), NomadParamDoc("Name of the save.")),
        },
        m_runtime->getBooleanType(),
        NomadDoc("Returns true if the save `<pref>/save/<saveName>.json` exists.")
    );

    m_runtime->registerNativeFunction(
        "game.settings.load",
        [this](const VirtualMachine* /*interpreter*/) {
            loadSettings();
        },
        { },
        m_runtime->getVoidType(),
        NomadDoc(
            "Loads `settings.*` variables from `<pref>/settings.json`. Variables absent from the file keep their "
            "current value."
        )
    );

    m_runtime->registerNativeFunction(
        "game.settings.save",
        [this](const VirtualMachine* /*interpreter*/) {
            saveSettings();
        },
        { },
        m_runtime->getVoidType(),
        NomadDoc("Saves all `settings.*` variables to `<pref>/settings.json`.")
    );

    m_runtime->registerNativeFunction(
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
        }, {
            defParameter("event", m_runtime->getEventDispatchType(), NomadParamDoc("The event to dispatch (trigger).")),
        },
        m_runtime->getVoidType(),
        NomadDoc("Trigger a global event.")
    );


}

} // namespace nomad
