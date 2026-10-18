// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/game/resource/AudioDiscovery.hpp>
#include <nomad/game/resource/Resource.hpp>

#include <SDL3_mixer/SDL_mixer.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace nomad {

class AudioManager;
class AudioObject;

class AudioException final : public ResourceException {
public:
    using ResourceException::ResourceException;
};

class AudioResource final : public Resource {
public:
    AudioResource(const NomadString& name, const NomadPath& filePath, MIX_Audio* audio);
    AudioResource(const AudioResource&) = delete;
    AudioResource& operator=(const AudioResource&) = delete;
    ~AudioResource() override;

    [[nodiscard]] const NomadPath& getFilePath() const;
    [[nodiscard]] MIX_Audio* getSdlAudio() const;

private:
    NomadPath m_filePath;
    MIX_Audio* m_audio;
};

class AudioManager final {
public:
    using CompletionCallback = std::function<void()>;

    AudioManager();
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;
    ~AudioManager();

    [[nodiscard]] bool isAvailable() const;

    [[nodiscard]] NomadIndex loadAudioRoots(
        const NomadPath& resourcePath,
        const std::vector<NomadPath>& directories,
        bool directoriesExplicitlyConfigured,
        bool missingDirectoriesAllowed = false
    );

    [[nodiscard]] NomadIndex getResourceCount() const;
    [[nodiscard]] const AudioResource* getResource(NomadId resourceId) const;
    [[nodiscard]] const AudioResource* getResourceByName(const NomadString& name) const;
    [[nodiscard]] NomadId getResourceId(const NomadString& name) const;

    [[nodiscard]] NomadId createAudioObject(NomadId resourceId);
    [[nodiscard]] NomadId createAudioObject(const NomadString& resourceName);
    [[nodiscard]] AudioObject* getAudioObject(NomadId audioObjectId);
    [[nodiscard]] const AudioObject* getAudioObject(NomadId audioObjectId) const;
    [[nodiscard]] NomadIndex getAudioObjectCount() const;

    void pauseAll();
    void resumeAll();
    void stopAll();

    [[nodiscard]] bool playMusic(
        const NomadPath& resourcePath,
        const NomadString& resourceName,
        bool loop,
        CompletionCallback callback = {}
    );
    void pauseMusic();
    void resumeMusic();
    void stopMusic();
    [[nodiscard]] bool isMusicPlaying() const;
    [[nodiscard]] bool isMusicPaused() const;
    [[nodiscard]] bool isMusicLooping() const;
    void processCallbacks();

    void setAudioPauseAutomatically(bool pauseAutomatically);
    [[nodiscard]] bool getAudioPauseAutomatically() const;
    void setMusicPauseAutomatically(bool pauseAutomatically);
    [[nodiscard]] bool getMusicPauseAutomatically() const;
    void pauseAutomatically();
    void resumeAutomatically();

    void setVolume(NomadFloat volume);
    [[nodiscard]] NomadFloat getVolume() const;
    void setMusicVolume(NomadFloat volume);
    [[nodiscard]] NomadFloat getMusicVolume() const;
    void setMasterVolume(NomadFloat volume);
    [[nodiscard]] NomadFloat getMasterVolume() const;

private:
    friend class AudioObject;
    void unregisterAudioObject(AudioObject* object);
    static void SDLCALL musicTrackStoppedCallback(void* userdata, MIX_Track* track);

    [[nodiscard]] bool ensureMusicTrack();
    void cancelMusicCompletion();
    void processMusicCompletion(std::vector<CompletionCallback>& callbacks);

    MIX_Mixer* m_mixer = nullptr;
    MIX_Track* m_musicTrack = nullptr;
    bool m_mixerInitialized = false;
    NomadFloat m_volume = 1.0;
    NomadFloat m_musicVolume = 1.0;
    NomadFloat m_masterVolume = 1.0;
    bool m_audioPauseAutomatically = true;
    bool m_musicPauseAutomatically = true;
    bool m_musicAutomaticallyPaused = false;
    bool m_musicLooping = false;
    NomadPath m_musicResourcePath;
    NomadString m_musicResourceName;
    std::atomic_bool m_musicCompletionArmed = false;
    std::atomic_bool m_musicCompletionPending = false;
    CompletionCallback m_musicCompletionCallback;
    std::vector<std::unique_ptr<AudioResource>> m_resources;
    std::vector<AudioObject*> m_audioObjects;
};

} // namespace nomad
