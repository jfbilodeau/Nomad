// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Game.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/game/resource/Audio.hpp>
#include <nomad/game/resource/AudioObject.hpp>
#include <nomad/game/resource/ResourceManager.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/Path.hpp>

namespace nomad {

namespace {

AudioManager::CompletionCallback createCompletionCallback(
    Game* game, const VirtualMachine* virtualMachine,
    const NomadIndex parameterIndex
) {
    auto closure = std::shared_ptr(
        virtualMachine->createClosure(
            game->getRuntime(),
            virtualMachine->getCallableIdParameter(parameterIndex)
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

AudioObject* getAudioObject(VirtualMachine* virtualMachine) {
    auto* audioObject = virtualMachine->getThisAs<AudioObject>();

    if (audioObject->getResource() == nullptr) {
        virtualMachine->fault("Audio resource is no longer available");
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

    const auto* audioType = static_cast<const ObjectType*>(m_runtime->getTypeByName("Audio"));
    if (audioType == nullptr) {
        raiseError("Audio type is not registered");
    }
    const auto audioTypeId = m_runtime->getTypeId("Audio");

    m_runtime->registerNativeFunction(
        "game.audio.load",
        [this](VirtualMachine* virtualMachine) {
            const auto& resourceName = virtualMachine->getStringParameter(0)->getString();
            auto* audio = m_resourceManager->getAudio();

            if (!audio->isAvailable()) {
                virtualMachine->fault("Audio playback is unavailable");
            }

            executeAudioAction(virtualMachine, [&] {
                auto* object = audio->createAudioObject(resourceName);
                if (object == nullptr) {
                    virtualMachine->fault("Failed to load Audio '" + resourceName + "'");
                }
                virtualMachine->getResult().setObjectValue(object);
            });
        },
        {
            defParameter(
                "name",
                m_runtime->getStringType(),
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

    const auto volumeId = m_runtime->addProperty(audioTypeId, {
        .valueTypeId = m_runtime->getTypeId("float"),
        .name = "volume",
        .getter = [](VirtualMachine* virtualMachine, RuntimeValue& result) {
            result.setFloatValue(getAudioObject(virtualMachine)->getVolume());
        },
        .setter = [](VirtualMachine* virtualMachine, const RuntimeValue& value) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(virtualMachine)->setVolume(value.getFloatValue());
            });
        },
        .documentation = NomadDoc("The sound-effect instance volume from 0.0 to 1.0.")
    });

    const auto playingId = m_runtime->addProperty(audioTypeId, {
        .valueTypeId = m_runtime->getTypeId("bool"),
        .name = "playing",
        .getter = [](VirtualMachine* virtualMachine, RuntimeValue& result) {
            result.setBooleanValue(getAudioObject(virtualMachine)->isPlaying());
        },
        .setter = {},
        .documentation = NomadDoc("Whether the sound effect is currently playing.")
    });

    const auto pausedId = m_runtime->addProperty(audioTypeId, {
        .valueTypeId = m_runtime->getTypeId("bool"),
        .name = "paused",
        .getter = [](VirtualMachine* virtualMachine, RuntimeValue& result) {
            result.setBooleanValue(getAudioObject(virtualMachine)->isPaused());
        },
        .setter = {},
        .documentation = NomadDoc("Whether the sound effect is currently paused.")
    });

    const auto loopingId = m_runtime->addProperty(audioTypeId, {
        .valueTypeId = m_runtime->getTypeId("bool"),
        .name = "looping",
        .getter = [](VirtualMachine* virtualMachine, RuntimeValue& result) {
            result.setBooleanValue(getAudioObject(virtualMachine)->isLooping());
        },
        .setter = {},
        .documentation = NomadDoc("Whether the sound effect will repeat indefinitely.")
    });
    if (volumeId.isInvalid() || playingId.isInvalid() || pausedId.isInvalid() || loopingId.isInvalid()) {
        raiseError("Failed to register Audio properties");
    }

    const auto playId = m_runtime->addMethod(audioTypeId, {
        .name = "play",
        .parameters = {},
        .returnTypeId = m_runtime->getTypeId("void"),
        .function = [](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(virtualMachine)->playOnce();
            });
        },
        .documentation = NomadDoc("Plays the sound effect once from the beginning.")
    });

    const auto playLoopId = m_runtime->addMethod(audioTypeId, {
        .name = "playLoop",
        .parameters = {},
        .returnTypeId = m_runtime->getTypeId("void"),
        .function = [](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(virtualMachine)->playLoop();
            });
        },
        .documentation = NomadDoc("Plays the sound effect repeatedly.")
    });

    const auto playOnceThenId = m_runtime->addMethod(audioTypeId, {
        .name = "playOnceThen",
        .parameters = {{
            m_runtime->getCallbackType({}, m_runtime->getVoidType())->getId(),
            "callback",
            NomadParamDoc("Function to execute after the sound effect finishes.")
        }},
        .returnTypeId = m_runtime->getTypeId("void"),
        .function = [this](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(virtualMachine)->playOnceThen(createCompletionCallback(this, virtualMachine, 0));
            });
        },
        .documentation = NomadDoc("Plays the sound effect once and invokes a callback when it finishes.")
    });

    const auto pauseId = m_runtime->addMethod(audioTypeId, {
        .name = "pause",
        .parameters = {},
        .returnTypeId = m_runtime->getTypeId("void"),
        .function = [](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(virtualMachine)->pause();
            });
        },
        .documentation = NomadDoc("Pauses the sound effect.")
    });

    const auto resumeId = m_runtime->addMethod(audioTypeId, {
        .name = "resume",
        .parameters = {},
        .returnTypeId = m_runtime->getTypeId("void"),
        .function = [](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(virtualMachine)->resume();
            });
        },
        .documentation = NomadDoc("Resumes the paused sound effect.")
    });

    const auto stopId = m_runtime->addMethod(audioTypeId, {
        .name = "stop",
        .parameters = {},
        .returnTypeId = m_runtime->getTypeId("void"),
        .function = [](VirtualMachine* virtualMachine) {
            executeAudioAction(virtualMachine, [&] {
                getAudioObject(virtualMachine)->stop();
            });
        },
        .documentation = NomadDoc("Stops the sound effect.")
    });
    if (playId.isInvalid() || playLoopId.isInvalid() || playOnceThenId.isInvalid() ||
        pauseId.isInvalid() || resumeId.isInvalid() || stopId.isInvalid()) {
        raiseError("Failed to register Audio methods");
    }

    m_runtime->registerNativeFunction(
        "game.music.play",
        [this](const VirtualMachine* virtualMachine) {
            const auto& resourceName = virtualMachine->getStringParameter(0)->getString();
            (void)m_resourceManager->getAudio()->playMusic(
                pathFromUtf8(m_options.resourcePath),
                resourceName,
                true
            );
        },
        {
            defParameter(
                "name",
                m_runtime->getStringType(),
                NomadParamDoc("The extensionless dotted name of a streamed music resource.")
            )
        },
        m_runtime->getVoidType(),
        NomadDoc("Streams and repeatedly plays a music resource from the music directory.")
    );

    m_runtime->registerNativeFunction(
        "game.music.playOnce",
        [this](VirtualMachine* virtualMachine) {
            const auto& resourceName = virtualMachine->getStringParameter(0)->getString();
            (void)m_resourceManager->getAudio()->playMusic(
                pathFromUtf8(m_options.resourcePath),
                resourceName,
                false
            );
        },
        {
            defParameter(
                "name",
                m_runtime->getStringType(),
                NomadParamDoc("The extensionless dotted name of a streamed music resource.")
            )
        },
        m_runtime->getVoidType(),
        NomadDoc("Streams and plays a music resource once from the music directory.")
    );

    m_runtime->registerNativeFunction(
        "game.music.playThen",
        [this](VirtualMachine* virtualMachine) {
            const auto& resourceName = virtualMachine->getStringParameter(0)->getString();
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
                m_runtime->getStringType(),
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
