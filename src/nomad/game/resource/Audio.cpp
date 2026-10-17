// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/resource/Audio.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/String.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
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

bool playTrack(MIX_Track* track, const NomadInteger loops) {
    const auto properties = SDL_CreateProperties();

    if (properties == 0) {
        log::error(getMixerError("Failed to create audio playback options"));
        return false;
    }

    if (!SDL_SetNumberProperty(properties, MIX_PROP_PLAY_LOOPS_NUMBER, loops)) {
        log::error(getMixerError("Failed to configure audio playback"));
        SDL_DestroyProperties(properties);
        return false;
    }

    const auto played = MIX_PlayTrack(track, properties);
    const auto message = played ? NomadString{} : getMixerError("Failed to play an audio track");
    SDL_DestroyProperties(properties);

    if (!played) {
        log::error(message);
    }

    return played;
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
    if (!MIX_SetTrackStoppedCallback(m_track, trackStoppedCallback, this)) {
        MIX_DestroyTrack(m_track);
        m_track = nullptr;
        throw AudioException(getMixerError("Failed to register the audio completion callback"));
    }

    if (!applyVolume()) {
        MIX_DestroyTrack(m_track);
        m_track = nullptr;
        throw AudioException("Failed to initialize audio track volume");
    }
}

AudioObject::~AudioObject() {
    cancelCompletion();
    (void)MIX_SetTrackStoppedCallback(m_track, nullptr, nullptr);
    MIX_DestroyTrack(m_track);
}

const AudioResource* AudioObject::getResource() const {
    return m_resource;
}

void AudioObject::playOnce() {
    play(0);
}

void AudioObject::playOnceThen(CompletionCallback callback) {
    play(0, std::move(callback));
}

void AudioObject::playLoop() {
    play(-1);
}

void AudioObject::pause() {
    m_automaticallyPaused = false;

    if (!MIX_PauseTrack(m_track)) {
        log::error(getMixerError("Failed to pause an audio track"));
    }
}

void AudioObject::resume() {
    m_automaticallyPaused = false;

    if (!MIX_ResumeTrack(m_track)) {
        log::error(getMixerError("Failed to resume an audio track"));
    }
}

void AudioObject::stop() {
    cancelCompletion();
    m_automaticallyPaused = false;

    if (!MIX_StopTrack(m_track, 0)) {
        log::error(getMixerError("Failed to stop an audio track"));
    }
}

void AudioObject::pauseAutomatically() {
    if (!isPlaying() || isPaused()) {
        return;
    }

    if (!MIX_PauseTrack(m_track)) {
        log::error(getMixerError("Failed to automatically pause an audio track"));
        return;
    }

    m_automaticallyPaused = true;
}

void AudioObject::resumeAutomatically() {
    if (!m_automaticallyPaused) {
        return;
    }

    m_automaticallyPaused = false;

    if (!MIX_ResumeTrack(m_track)) {
        log::error(getMixerError("Failed to automatically resume an audio track"));
    }
}

AudioObject::CompletionCallback AudioObject::takeCompletionCallback() {
    if (!m_completionPending.exchange(false, std::memory_order_acq_rel)) {
        return {};
    }

    auto callback = std::move(m_completionCallback);
    m_completionCallback = {};
    return callback;
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

void AudioObject::trackStoppedCallback(void* userdata, MIX_Track* /*track*/) {
    auto* audioObject = static_cast<AudioObject*>(userdata);

    if (audioObject->m_completionArmed.exchange(false, std::memory_order_acq_rel)) {
        audioObject->m_completionPending.store(true, std::memory_order_release);
    }
}

void AudioObject::play(const NomadInteger loops, CompletionCallback callback) {
    cancelCompletion();
    m_automaticallyPaused = false;

    if (isPlaying() || isPaused()) {
        if (!MIX_StopTrack(m_track, 0)) {
            log::error(getMixerError("Failed to restart an audio track"));
            return;
        }
    }

    if (callback) {
        m_completionCallback = std::move(callback);
        m_completionArmed.store(true, std::memory_order_release);
    }

    if (!playTrack(m_track, loops)) {
        cancelCompletion();
    }
}

void AudioObject::cancelCompletion() {
    m_completionArmed.store(false, std::memory_order_release);
    m_completionPending.store(false, std::memory_order_release);
    m_completionCallback = {};
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

    if (m_musicTrack != nullptr) {
        cancelMusicCompletion();
        (void)MIX_SetTrackStoppedCallback(m_musicTrack, nullptr, nullptr);
        MIX_DestroyTrack(m_musicTrack);
        m_musicTrack = nullptr;
    }

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

bool AudioManager::playMusic(
    const NomadPath& resourcePath,
    const NomadString& resourceName,
    const bool loop,
    CompletionCallback callback
) {
    if (!ensureMusicTrack()) {
        return false;
    }

    std::vector<AudioSourceDefinition> sources;

    try {
        sources = discoverAudioSources(resourcePath, {"music"}, false);
    } catch (const AudioDiscoveryException& error) {
        log::error(error.what());
        return false;
    }

    const auto source = std::ranges::find_if(sources, [&](const auto& candidate) {
        return candidate.name == resourceName;
    });

    if (source == sources.end()) {
        log::error("Music resource does not exist: '" + resourceName + "'");
        return false;
    }

    auto* stream = SDL_IOFromFile(pathToUtf8(source->filePath).c_str(), "rb");

    if (stream == nullptr) {
        log::error(
            "Failed to open music resource '" + resourceName + "' from '" +
            pathToUtf8(source->filePath) + "': " + SDL_GetError()
        );
        return false;
    }

    cancelMusicCompletion();
    m_musicAutomaticallyPaused = false;

    if (!MIX_StopTrack(m_musicTrack, 0)) {
        log::error(getMixerError("Failed to stop the current music track"));
        SDL_CloseIO(stream);
        return false;
    }

    if (!MIX_SetTrackIOStream(m_musicTrack, stream, true)) {
        log::error(getMixerError("Failed to stream music resource '" + resourceName + "'"));
        SDL_CloseIO(stream);
        return false;
    }

    m_musicLooping = loop;
    m_musicResourcePath = resourcePath;
    m_musicResourceName = resourceName;
    m_musicCompletionCallback = std::move(callback);
    m_musicCompletionArmed.store(true, std::memory_order_release);

    if (!playTrack(m_musicTrack, 0)) {
        cancelMusicCompletion();
        (void)MIX_SetTrackIOStream(m_musicTrack, nullptr, false);
        return false;
    }

    return true;
}

void AudioManager::pauseMusic() {
    m_musicAutomaticallyPaused = false;

    if (m_musicTrack != nullptr && !MIX_PauseTrack(m_musicTrack)) {
        log::error(getMixerError("Failed to pause the music track"));
    }
}

void AudioManager::resumeMusic() {
    m_musicAutomaticallyPaused = false;

    if (m_musicTrack != nullptr && !MIX_ResumeTrack(m_musicTrack)) {
        log::error(getMixerError("Failed to resume the music track"));
    }
}

void AudioManager::stopMusic() {
    if (m_musicTrack == nullptr) {
        return;
    }

    cancelMusicCompletion();
    m_musicAutomaticallyPaused = false;
    m_musicLooping = false;
    m_musicResourcePath.clear();
    m_musicResourceName.clear();

    if (!MIX_StopTrack(m_musicTrack, 0)) {
        log::error(getMixerError("Failed to stop the music track"));
    }

    if (!MIX_SetTrackIOStream(m_musicTrack, nullptr, false)) {
        log::error(getMixerError("Failed to release the music stream"));
    }
}

bool AudioManager::isMusicPlaying() const {
    return m_musicTrack != nullptr && MIX_TrackPlaying(m_musicTrack);
}

bool AudioManager::isMusicPaused() const {
    return m_musicTrack != nullptr && MIX_TrackPaused(m_musicTrack);
}

bool AudioManager::isMusicLooping() const {
    return m_musicLooping;
}

void AudioManager::processCallbacks() {
    std::vector<CompletionCallback> callbacks;

    for (auto& audioObject : m_audioObjects) {
        auto callback = audioObject->takeCompletionCallback();

        if (callback) {
            callbacks.push_back(std::move(callback));
        }
    }

    processMusicCompletion(callbacks);

    for (auto& callback : callbacks) {
        callback();
    }
}

void AudioManager::setAudioPauseAutomatically(const bool pauseAutomatically) {
    m_audioPauseAutomatically = pauseAutomatically;
}

bool AudioManager::getAudioPauseAutomatically() const {
    return m_audioPauseAutomatically;
}

void AudioManager::setMusicPauseAutomatically(const bool pauseAutomatically) {
    m_musicPauseAutomatically = pauseAutomatically;
}

bool AudioManager::getMusicPauseAutomatically() const {
    return m_musicPauseAutomatically;
}

void AudioManager::pauseAutomatically() {
    if (m_audioPauseAutomatically) {
        for (auto& audioObject : m_audioObjects) {
            audioObject->pauseAutomatically();
        }
    }

    if (
        m_musicPauseAutomatically &&
        m_musicTrack != nullptr &&
        isMusicPlaying() &&
        !isMusicPaused()
    ) {
        if (!MIX_PauseTrack(m_musicTrack)) {
            log::error(getMixerError("Failed to automatically pause the music track"));
        } else {
            m_musicAutomaticallyPaused = true;
        }
    }
}

void AudioManager::resumeAutomatically() {
    for (auto& audioObject : m_audioObjects) {
        audioObject->resumeAutomatically();
    }

    if (m_musicAutomaticallyPaused) {
        m_musicAutomaticallyPaused = false;

        if (!MIX_ResumeTrack(m_musicTrack)) {
            log::error(getMixerError("Failed to automatically resume the music track"));
        }
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

void AudioManager::setMusicVolume(const NomadFloat volume) {
    const auto clampedVolume = clampVolume(volume);

    if (
        m_musicTrack != nullptr &&
        !MIX_SetTrackGain(m_musicTrack, static_cast<float>(clampedVolume))
    ) {
        log::error(getMixerError("Failed to set music volume"));
        return;
    }

    m_musicVolume = clampedVolume;
}

NomadFloat AudioManager::getMusicVolume() const {
    return m_musicVolume;
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

bool AudioManager::ensureMusicTrack() {
    if (!isAvailable()) {
        log::error("Cannot play music because audio playback is unavailable");
        return false;
    }

    if (m_musicTrack != nullptr) {
        return true;
    }

    m_musicTrack = MIX_CreateTrack(m_mixer);

    if (m_musicTrack == nullptr) {
        log::error(getMixerError("Failed to create the music track"));
        return false;
    }

    if (!MIX_SetTrackGain(m_musicTrack, static_cast<float>(m_musicVolume))) {
        log::error(getMixerError("Failed to initialize music volume"));
        MIX_DestroyTrack(m_musicTrack);
        m_musicTrack = nullptr;
        return false;
    }
    if (!MIX_SetTrackStoppedCallback(m_musicTrack, musicTrackStoppedCallback, this)) {
        log::error(getMixerError("Failed to register the music completion callback"));
        MIX_DestroyTrack(m_musicTrack);
        m_musicTrack = nullptr;
        return false;
    }

    return true;
}

void AudioManager::musicTrackStoppedCallback(void* userdata, MIX_Track* /*track*/) {
    auto* audioManager = static_cast<AudioManager*>(userdata);

    if (audioManager->m_musicCompletionArmed.exchange(false, std::memory_order_acq_rel)) {
        audioManager->m_musicCompletionPending.store(true, std::memory_order_release);
    }
}

void AudioManager::cancelMusicCompletion() {
    m_musicCompletionArmed.store(false, std::memory_order_release);
    m_musicCompletionPending.store(false, std::memory_order_release);
    m_musicCompletionCallback = {};
}

void AudioManager::processMusicCompletion(std::vector<CompletionCallback>& callbacks) {
    if (!m_musicCompletionPending.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    auto callback = std::move(m_musicCompletionCallback);
    m_musicCompletionCallback = {};

    if (m_musicLooping) {
        const auto resourcePath = m_musicResourcePath;
        const auto resourceName = m_musicResourceName;

        if (!playMusic(resourcePath, resourceName, true)) {
            m_musicLooping = false;
            m_musicResourcePath.clear();
            m_musicResourceName.clear();
            (void)MIX_SetTrackIOStream(m_musicTrack, nullptr, false);
        }

        return;
    }

    m_musicResourcePath.clear();
    m_musicResourceName.clear();
    (void)MIX_SetTrackIOStream(m_musicTrack, nullptr, false);

    if (callback) {
        callbacks.push_back(std::move(callback));
    }
}

} // namespace nomad
