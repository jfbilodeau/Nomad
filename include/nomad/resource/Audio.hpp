// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/resource/AudioDiscovery.hpp>
#include <nomad/resource/Resource.hpp>

#include <SDL3_mixer/SDL_mixer.h>

#include <memory>
#include <vector>

namespace nomad {

class AudioManager;

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

class AudioObject final {
public:
    AudioObject(const AudioResource* resource, MIX_Mixer* mixer, NomadFloat categoryVolume);
    AudioObject(const AudioObject&) = delete;
    AudioObject& operator=(const AudioObject&) = delete;
    ~AudioObject();

    [[nodiscard]] const AudioResource* getResource() const;

    void playOnce();
    void playLoop();
    void pause();
    void resume();
    void stop();

    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isPaused() const;
    [[nodiscard]] bool isLooping() const;

    void setVolume(NomadFloat volume);
    [[nodiscard]] NomadFloat getVolume() const;

private:
    friend class AudioManager;

    void play(NomadInteger loops);
    void setCategoryVolume(NomadFloat volume);
    void applyVolume();

    const AudioResource* m_resource;
    MIX_Track* m_track;
    NomadFloat m_volume = 1.0;
    NomadFloat m_categoryVolume;
};

class AudioManager final {
public:
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
    void setVolume(NomadFloat volume);
    [[nodiscard]] NomadFloat getVolume() const;
    void setMasterVolume(NomadFloat volume);
    [[nodiscard]] NomadFloat getMasterVolume() const;

private:
    MIX_Mixer* m_mixer = nullptr;
    bool m_mixerInitialized = false;
    NomadFloat m_volume = 1.0;
    NomadFloat m_masterVolume = 1.0;
    std::vector<std::unique_ptr<AudioResource>> m_resources;
    std::vector<std::unique_ptr<AudioObject>> m_audioObjects;
};

} // namespace nomad
