// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/Audio.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/String.hpp>

#include <SDL3/SDL.h>

#include <cmath>
#include <set>
#include <utility>

namespace nomad {

namespace {

NomadFloat clampVolume(const NomadFloat volume) {
    if (!std::isfinite(volume)) {
        log::warning("Audio volume must be finite; clamping to the valid range");

        if (std::isnan(volume)) {
            return 0.0;
        }
    }

    if (volume < 0.0) {
        log::warning("Audio volume cannot be less than 0.0; clamping to 0.0");
        return 0.0;
    }
    if (volume > 1.0) {
        log::warning("Audio volume cannot be greater than 1.0; clamping to 1.0");
        return 1.0;
    }

    return volume;
}

NomadString getMixerError(const NomadStringView operation) {
    return NomadString(operation) + ": " + SDL_GetError();
}

} // namespace

AudioResource::AudioResource(
    const NomadString& name,
    const NomadPath& filePath,
    MIX_Audio* audio
):
    Resource(name),
    m_filePath(filePath),
    m_audio(audio)
{
    if (m_audio == nullptr) {
        throw AudioException("Cannot create an audio resource without decoded audio");
    }
}

AudioResource::~AudioResource() {
    MIX_DestroyAudio(m_audio);
}

const NomadPath& AudioResource::getFilePath() const {
    return m_filePath;
}

MIX_Audio* AudioResource::getSdlAudio() const {
    return m_audio;
}

AudioObject::AudioObject(
    const AudioResource* resource,
    MIX_Mixer* mixer,
    const NomadFloat categoryVolume
):
    m_resource(resource),
    m_track(MIX_CreateTrack(mixer)),
    m_categoryVolume(categoryVolume)
{
    if (m_resource == nullptr) {
        throw AudioException("Cannot create an audio object without a resource");
    }
    if (m_track == nullptr) {
        throw AudioException(getMixerError("Failed to create an audio track"));
    }
    if (!MIX_SetTrackAudio(m_track, m_resource->getSdlAudio())) {
        MIX_DestroyTrack(m_track);
        m_track = nullptr;
        throw AudioException(getMixerError("Failed to assign audio to a track"));
    }

    if (!applyVolume()) {
        MIX_DestroyTrack(m_track);
        m_track = nullptr;
        throw AudioException("Failed to initialize audio track volume");
    }
}

AudioObject::~AudioObject() {
    MIX_DestroyTrack(m_track);
}

const AudioResource* AudioObject::getResource() const {
    return m_resource;
}

void AudioObject::playOnce() {
    play(0);
}

void AudioObject::playLoop() {
    play(-1);
}

void AudioObject::pause() {
    if (!MIX_PauseTrack(m_track)) {
        log::error(getMixerError("Failed to pause an audio track"));
    }
}

void AudioObject::resume() {
    if (!MIX_ResumeTrack(m_track)) {
        log::error(getMixerError("Failed to resume an audio track"));
    }
}

void AudioObject::stop() {
    if (!MIX_StopTrack(m_track, 0)) {
        log::error(getMixerError("Failed to stop an audio track"));
    }
}

bool AudioObject::isPlaying() const {
    return MIX_TrackPlaying(m_track);
}

bool AudioObject::isPaused() const {
    return MIX_TrackPaused(m_track);
}

bool AudioObject::isLooping() const {
    return MIX_GetTrackLoops(m_track) != 0;
}

void AudioObject::setVolume(const NomadFloat volume) {
    const auto previousVolume = m_volume;
    m_volume = clampVolume(volume);

    if (!applyVolume()) {
        m_volume = previousVolume;
    }
}

NomadFloat AudioObject::getVolume() const {
    return m_volume;
}

void AudioObject::setCategoryVolume(const NomadFloat volume) {
    const auto previousVolume = m_categoryVolume;
    m_categoryVolume = clampVolume(volume);

    if (!applyVolume()) {
        m_categoryVolume = previousVolume;
    }
}

bool AudioObject::applyVolume() {
    if (!MIX_SetTrackGain(m_track, static_cast<float>(m_volume * m_categoryVolume))) {
        log::error(getMixerError("Failed to set audio track volume"));
        return false;
    }

    return true;
}

void AudioObject::play(const NomadInteger loops) {
    const auto properties = SDL_CreateProperties();

    if (properties == 0) {
        log::error(getMixerError("Failed to create audio playback options"));
        return;
    }

    if (!SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOPS_NUMBER, loops)) {
        log::error(getMixerError("Failed to configure audio playback"));
        SDL_DestroyProperties(properties);
        return;
    }

    const auto played = MIX_PlayTrack(m_track, properties);
    const auto message = played ? NomadString{} : getMixerError("Failed to play an audio track");
    SDL_DestroyProperties(properties);

    if (!played) {
        log::error(message);
    }
}

AudioManager::AudioManager() {
    if (!SDL_WasInit(SDL_INIT_AUDIO)) {
        log::warning("Audio playback is unavailable because the SDL audio subsystem is not initialized");
        return;
    }

    if (!MIX_Init()) {
        log::warning(getMixerError("Failed to initialize SDL_mixer"));
        return;
    }

    m_mixerInitialized = true;
    m_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);

    if (m_mixer == nullptr) {
        log::warning(getMixerError("Failed to create the audio mixer"));
        MIX_Quit();
        m_mixerInitialized = false;
    }
}

AudioManager::~AudioManager() {
    m_audioObjects.clear();
    m_resources.clear();

    if (m_mixer != nullptr) {
        MIX_DestroyMixer(m_mixer);
        m_mixer = nullptr;
    }

    if (m_mixerInitialized) {
        MIX_Quit();
        m_mixerInitialized = false;
    }
}

bool AudioManager::isAvailable() const {
    return m_mixer != nullptr;
}

NomadIndex AudioManager::loadAudioRoots(
    const NomadPath& resourcePath,
    const std::vector<NomadPath>& directories,
    const bool directoriesExplicitlyConfigured,
    const bool missingDirectoriesAllowed
) {
    if (!isAvailable()) {
        log::error("Cannot load audio resources because audio playback is unavailable");
        return 0;
    }

    const auto sources = discoverAudioSources(
        resourcePath,
        directories,
        directoriesExplicitlyConfigured && !missingDirectoriesAllowed
    );
    std::set<NomadString> resourceNames;

    for (const auto& resource : m_resources) {
        resourceNames.insert(stringToLowerAscii(resource->getName()));
    }

    std::vector<std::unique_ptr<AudioResource>> resources;
    resources.reserve(sources.size());

    for (const auto& source : sources) {
        if (!resourceNames.insert(stringToLowerAscii(source.name)).second) {
            log::error("Audio resource name is already registered: '" + source.name + "'");
            return 0;
        }

        auto* audio = MIX_LoadAudio(m_mixer, pathToUtf8(source.filePath).c_str(), true);

        if (audio == nullptr) {
            log::error(
                "Failed to preload audio resource '" + source.name + "' from '" +
                pathToUtf8(source.filePath) + "': " + SDL_GetError()
            );
            return 0;
        }

        resources.push_back(std::make_unique<AudioResource>(source.name, source.filePath, audio));
    }

    m_resources.reserve(m_resources.size() + resources.size());

    for (auto& resource : resources) {
        m_resources.push_back(std::move(resource));
    }

    return sources.size();
}

NomadIndex AudioManager::getResourceCount() const {
    return m_resources.size();
}

const AudioResource* AudioManager::getResource(const NomadId resourceId) const {
    const auto index = toNomadIndex(resourceId);
    return index < m_resources.size() ? m_resources[index].get() : nullptr;
}

const AudioResource* AudioManager::getResourceByName(const NomadString& name) const {
    const auto id = getResourceId(name);
    return id == NOMAD_INVALID_ID ? nullptr : getResource(id);
}

NomadId AudioManager::getResourceId(const NomadString& name) const {
    for (NomadIndex index = 0; index < m_resources.size(); ++index) {
        if (m_resources[index]->getName() == name) {
            return toNomadId(index);
        }
    }

    return NOMAD_INVALID_ID;
}

NomadId AudioManager::createAudioObject(const NomadId resourceId) {
    const auto* resource = getResource(resourceId);

    if (resource == nullptr) {
        log::error("Cannot create an audio object for an invalid resource ID");
        return NOMAD_INVALID_ID;
    }

    const auto audioObjectId = toNomadId(m_audioObjects.size());
    try {
        m_audioObjects.push_back(std::make_unique<AudioObject>(resource, m_mixer, m_volume));
    } catch (const AudioException& error) {
        log::error(error.what());
        return NOMAD_INVALID_ID;
    }

    return audioObjectId;
}

NomadId AudioManager::createAudioObject(const NomadString& resourceName) {
    const auto resourceId = getResourceId(resourceName);

    if (resourceId == NOMAD_INVALID_ID) {
        log::error("Audio resource does not exist: '" + resourceName + "'");
        return NOMAD_INVALID_ID;
    }

    return createAudioObject(resourceId);
}

AudioObject* AudioManager::getAudioObject(const NomadId audioObjectId) {
    const auto index = toNomadIndex(audioObjectId);
    return index < m_audioObjects.size() ? m_audioObjects[index].get() : nullptr;
}

const AudioObject* AudioManager::getAudioObject(const NomadId audioObjectId) const {
    const auto index = toNomadIndex(audioObjectId);
    return index < m_audioObjects.size() ? m_audioObjects[index].get() : nullptr;
}

NomadIndex AudioManager::getAudioObjectCount() const {
    return m_audioObjects.size();
}

void AudioManager::pauseAll() {
    for (auto& audioObject : m_audioObjects) {
        audioObject->pause();
    }
}

void AudioManager::resumeAll() {
    for (auto& audioObject : m_audioObjects) {
        audioObject->resume();
    }
}

void AudioManager::stopAll() {
    for (auto& audioObject : m_audioObjects) {
        audioObject->stop();
    }
}

void AudioManager::setVolume(const NomadFloat volume) {
    const auto clampedVolume = clampVolume(volume);

    for (auto& audioObject : m_audioObjects) {
        audioObject->setCategoryVolume(clampedVolume);
    }

    m_volume = clampedVolume;
}

NomadFloat AudioManager::getVolume() const {
    return m_volume;
}

void AudioManager::setMasterVolume(const NomadFloat volume) {
    const auto clampedVolume = clampVolume(volume);

    if (isAvailable() && !MIX_SetMixerGain(m_mixer, static_cast<float>(clampedVolume))) {
        log::error(getMixerError("Failed to set master audio volume"));
        return;
    }

    m_masterVolume = clampedVolume;
}

NomadFloat AudioManager::getMasterVolume() const {
    return m_masterVolume;
}

} // namespace nomad
