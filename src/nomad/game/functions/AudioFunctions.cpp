// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/resource/Audio.hpp>
#include <nomad/resource/ResourceManager.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

namespace nomad {

namespace {

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

    const auto* audioType = m_runtime->registerOpaqueIdType(
        "audio",
        NomadDoc("A controllable instance of a preloaded sound effect.")
    );

    if (audioType == nullptr) {
        raiseError("Failed to register the audio type");
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
}

} // namespace nomad
