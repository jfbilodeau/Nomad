// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/resource/Audio.hpp>
#include <nomad/resource/ResourceManager.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/Path.hpp>

namespace nomad {

namespace {

AudioManager::CompletionCallback createCompletionCallback(
    Game* game,
    VirtualMachine* virtualMachine,
    const NomadIndex parameterIndex
) {
    auto closure = std::shared_ptr<Closure>(
        virtualMachine->createClosure(
            game->getRuntime(),
            virtualMachine->getIdParameter(parameterIndex)
        )
    );
    const auto* context = game->getCurrentContext();
    const auto* scene = context->getScene();
    const auto* entity = context->getThisEntity();
    const auto sceneId = scene == nullptr ? NOMAD_INVALID_ID : scene->getId();
    const auto entityId = entity == nullptr ? NOMAD_INVALID_ID : entity->getId();

    return [game, closure = std::move(closure), sceneId, entityId] {
        auto* callbackScene =
            sceneId == NOMAD_INVALID_ID ? nullptr : game->getSceneById(sceneId);

        if (sceneId != NOMAD_INVALID_ID && callbackScene == nullptr) {
            log::warning("Audio completion callback scene no longer exists");
            return;
        }

        auto* callbackEntity =
            entityId == NOMAD_INVALID_ID || callbackScene == nullptr
                ? nullptr
                : callbackScene->getEntityById(entityId);

        if (entityId != NOMAD_INVALID_ID && callbackEntity == nullptr) {
            log::warning("Audio completion callback entity no longer exists");
            return;
        }

        game->executeFunction(closure.get(), callbackScene, callbackEntity);
    };
}

AudioObject* getAudioObject(
    Game* game,
    VirtualMachine* virtualMachine,
    const RuntimeValue& receiver
) {
    auto* audioObject = game->getResources()->getAudio()->getAudioObject(receiver.getIdValue());

    if (audioObject == nullptr) {
        virtualMachine->fault("Invalid audio object");
    }

    return audioObject;
}

template<typename Fn>
void executeAudioAction(VirtualMachine* virtualMachine, Fn&& action) {
    try {
        action();
    } catch (const AudioException& error) {
        virtualMachine->fault(error.what());
    }
}

} // namespace

void Game::initAudioFunctions() {
    log::debug("Initializing audio functions");

    const auto* audioType = m_runtime->getTypeByName("audio");
    if (audioType == nullptr) {
        raiseError("Audio type is not registered");
    }

    m_runtime->registerNativeFunction(
        "game.audio.load",
        [this](VirtualMachine* virtualMachine) {
            const auto resourceName = NomadString(virtualMachine->getStringParameter(0));
            auto* audio = m_resourceManager->getAudio();

            if (!audio->isAvailable()) {
                log::error("Audio playback is unavailable");
                virtualMachine->setIdResult(NOMAD_INVALID_ID);
                return;
            }

            executeAudioAction(virtualMachine, [&] {
                virtualMachine->setIdResult(audio->createAudioObject(resourceName));
            });
        },
        {
            defParameter(
                "name",
                m_runtime->getStringRefType(),
                NomadParamDoc("The extensionless dotted name of a preloaded sound effect.")
            )
        },
        audioType,
        NomadDoc("Creates a controllable audio instance for a preloaded sound effect.")
    );

    m_runtime->registerNativeFunction(
        "game.audio.pauseAll",
        [this](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [this] {
                m_resourceManager->getAudio()->pauseAll();
            });
        },
        {},
        m_runtime->getVoidType(),
        NomadDoc("Pauses every playing sound effect.")
    );

    m_runtime->registerNativeFunction(
        "game.audio.resumeAll",
        [this](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [this] {
                m_resourceManager->getAudio()->resumeAll();
            });
        },
        {},
        m_runtime->getVoidType(),
        NomadDoc("Resumes every paused sound effect.")
    );

    m_runtime->registerNativeFunction(
        "game.audio.stopAll",
        [this](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [this] {
                m_resourceManager->getAudio()->stopAll();
            });
        },
        {},
        m_runtime->getVoidType(),
        NomadDoc("Stops every playing or paused sound effect.")
    );

    m_runtime->registerIntrinsicProperty(
        audioType,
        "volume",
        m_runtime->getFloatType(),
        [this](VirtualMachine* virtualMachine, const RuntimeValue& receiver, RuntimeValue& result) {
            result.setFloatValue(getAudioObject(this, virtualMachine, receiver)->getVolume());
        },
        [this](VirtualMachine* virtualMachine, RuntimeValue& receiver, const RuntimeValue& value) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(this, virtualMachine, receiver)->setVolume(value.getFloatValue());
            });
        },
        NomadDoc("The sound-effect instance volume from 0.0 to 1.0.")
    );

    m_runtime->registerIntrinsicProperty(
        audioType,
        "playing",
        m_runtime->getBooleanType(),
        [this](VirtualMachine* virtualMachine, const RuntimeValue& receiver, RuntimeValue& result) {
            result.setBooleanValue(getAudioObject(this, virtualMachine, receiver)->isPlaying());
        },
        {},
        NomadDoc("Whether the sound effect is currently playing.")
    );

    m_runtime->registerIntrinsicProperty(
        audioType,
        "paused",
        m_runtime->getBooleanType(),
        [this](VirtualMachine* virtualMachine, const RuntimeValue& receiver, RuntimeValue& result) {
            result.setBooleanValue(getAudioObject(this, virtualMachine, receiver)->isPaused());
        },
        {},
        NomadDoc("Whether the sound effect is currently paused.")
    );

    m_runtime->registerIntrinsicProperty(
        audioType,
        "looping",
        m_runtime->getBooleanType(),
        [this](VirtualMachine* virtualMachine, const RuntimeValue& receiver, RuntimeValue& result) {
            result.setBooleanValue(getAudioObject(this, virtualMachine, receiver)->isLooping());
        },
        {},
        NomadDoc("Whether the sound effect will repeat indefinitely.")
    );

    m_runtime->registerIntrinsicMethod(
        audioType,
        "play",
        [this](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(
                    this,
                    virtualMachine,
                    virtualMachine->getIntrinsicReceiver()
                )->playOnce();
            });
        },
        {},
        m_runtime->getVoidType(),
        false,
        NomadDoc("Plays the sound effect once from the beginning.")
    );

    m_runtime->registerIntrinsicMethod(
        audioType,
        "playLoop",
        [this](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(
                    this,
                    virtualMachine,
                    virtualMachine->getIntrinsicReceiver()
                )->playLoop();
            });
        },
        {},
        m_runtime->getVoidType(),
        false,
        NomadDoc("Plays the sound effect repeatedly.")
    );

    m_runtime->registerIntrinsicMethod(
        audioType,
        "playOnceThen",
        [this](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(
                    this,
                    virtualMachine,
                    virtualMachine->getIntrinsicReceiver()
                )->playOnceThen(createCompletionCallback(this, virtualMachine, 0));
            });
        },
        {
            defParameter(
                "callback",
                m_runtime->getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("Function to execute after the sound effect finishes.")
            )
        },
        m_runtime->getVoidType(),
        false,
        NomadDoc("Plays the sound effect once and invokes a callback when it finishes.")
    );

    m_runtime->registerIntrinsicMethod(
        audioType,
        "pause",
        [this](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(
                    this,
                    virtualMachine,
                    virtualMachine->getIntrinsicReceiver()
                )->pause();
            });
        },
        {},
        m_runtime->getVoidType(),
        false,
        NomadDoc("Pauses the sound effect.")
    );

    m_runtime->registerIntrinsicMethod(
        audioType,
        "resume",
        [this](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(
                    this,
                    virtualMachine,
                    virtualMachine->getIntrinsicReceiver()
                )->resume();
            });
        },
        {},
        m_runtime->getVoidType(),
        false,
        NomadDoc("Resumes the paused sound effect.")
    );

    m_runtime->registerIntrinsicMethod(
        audioType,
        "stop",
        [this](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(
                    this,
                    virtualMachine,
                    virtualMachine->getIntrinsicReceiver()
                )->stop();
            });
        },
        {},
        m_runtime->getVoidType(),
        false,
        NomadDoc("Stops the sound effect.")
    );

    m_runtime->registerNativeFunction(
        "game.music.play",
        [this](VirtualMachine* virtualMachine) {
            const auto resourceName = NomadString(virtualMachine->getStringParameter(0));
            (void)m_resourceManager->getAudio()->playMusic(
                pathFromUtf8(m_options.resourcePath),
                resourceName,
                true
            );
        },
        {
            defParameter(
                "name",
                m_runtime->getStringRefType(),
                NomadParamDoc("The extensionless dotted name of a streamed music resource.")
            )
        },
        m_runtime->getVoidType(),
        NomadDoc("Streams and repeatedly plays a music resource from the music directory.")
    );

    m_runtime->registerNativeFunction(
        "game.music.playOnce",
        [this](VirtualMachine* virtualMachine) {
            const auto resourceName = NomadString(virtualMachine->getStringParameter(0));
            (void)m_resourceManager->getAudio()->playMusic(
                pathFromUtf8(m_options.resourcePath),
                resourceName,
                false
            );
        },
        {
            defParameter(
                "name",
                m_runtime->getStringRefType(),
                NomadParamDoc("The extensionless dotted name of a streamed music resource.")
            )
        },
        m_runtime->getVoidType(),
        NomadDoc("Streams and plays a music resource once from the music directory.")
    );

    m_runtime->registerNativeFunction(
        "game.music.playThen",
        [this](VirtualMachine* virtualMachine) {
            const auto resourceName = NomadString(virtualMachine->getStringParameter(0));
            (void)m_resourceManager->getAudio()->playMusic(
                pathFromUtf8(m_options.resourcePath),
                resourceName,
                false,
                createCompletionCallback(this, virtualMachine, 1)
            );
        },
        {
            defParameter(
                "name",
                m_runtime->getStringRefType(),
                NomadParamDoc("The extensionless dotted name of a streamed music resource.")
            ),
            defParameter(
                "callback",
                m_runtime->getCallbackType({}, m_runtime->getVoidType()),
                NomadParamDoc("Function to execute after the music finishes.")
            )
        },
        m_runtime->getVoidType(),
        NomadDoc("Streams a music resource once and invokes a callback when it finishes.")
    );

    m_runtime->registerNativeFunction(
        "game.music.pause",
        [this](VirtualMachine* /*virtualMachine*/) {
            m_resourceManager->getAudio()->pauseMusic();
        },
        {},
        m_runtime->getVoidType(),
        NomadDoc("Pauses the current music track.")
    );

    m_runtime->registerNativeFunction(
        "game.music.resume",
        [this](VirtualMachine* /*virtualMachine*/) {
            m_resourceManager->getAudio()->resumeMusic();
        },
        {},
        m_runtime->getVoidType(),
        NomadDoc("Resumes the paused music track.")
    );

    m_runtime->registerNativeFunction(
        "game.music.stop",
        [this](VirtualMachine* /*virtualMachine*/) {
            m_resourceManager->getAudio()->stopMusic();
        },
        {},
        m_runtime->getVoidType(),
        NomadDoc("Stops the current music track and releases its stream.")
    );
}

} // namespace nomad
