// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/resource/AudioObject.hpp>

#include <nomad/game/resource/Audio.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/game/resource/AudioPlayback.hpp>

#include <utility>

namespace nomad {

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
        throw AudioException(audio::getMixerError("Failed to create an audio track"));
    }
    if (!MIX_SetTrackAudio(m_track, m_resource->getSdlAudio())) {
        MIX_DestroyTrack(m_track);
        m_track = nullptr;
        throw AudioException(audio::getMixerError("Failed to assign audio to a track"));
    }
    if (!MIX_SetTrackStoppedCallback(m_track, trackStoppedCallback, this)) {
        MIX_DestroyTrack(m_track);
        m_track = nullptr;
        throw AudioException(audio::getMixerError("Failed to register the audio completion callback"));
    }
    if (!applyVolume()) {
        MIX_DestroyTrack(m_track);
        m_track = nullptr;
        throw AudioException("Failed to initialize audio track volume");
    }
}

AudioObject::~AudioObject() {
    if (m_manager != nullptr) {
        m_manager->unregisterAudioObject(this);
    }
    detachManager();
}

void AudioObject::detachManager() {
    cancelCompletion();
    if (m_track != nullptr) {
        (void)MIX_SetTrackStoppedCallback(m_track, nullptr, nullptr);
        MIX_DestroyTrack(m_track);
        m_track = nullptr;
    }
    m_manager = nullptr;
    m_resource = nullptr;
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
        log::error(audio::getMixerError("Failed to pause an audio track"));
    }
}

void AudioObject::resume() {
    m_automaticallyPaused = false;
    if (!MIX_ResumeTrack(m_track)) {
        log::error(audio::getMixerError("Failed to resume an audio track"));
    }
}

void AudioObject::stop() {
    cancelCompletion();
    m_automaticallyPaused = false;
    if (!MIX_StopTrack(m_track, 0)) {
        log::error(audio::getMixerError("Failed to stop an audio track"));
    }
}

void AudioObject::pauseAutomatically() {
    if (!isPlaying() || isPaused()) {
        return;
    }
    if (!MIX_PauseTrack(m_track)) {
        log::error(audio::getMixerError("Failed to automatically pause an audio track"));
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
        log::error(audio::getMixerError("Failed to automatically resume an audio track"));
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
    return m_track != nullptr && MIX_TrackPlaying(m_track);
}

bool AudioObject::isPaused() const {
    return m_track != nullptr && MIX_TrackPaused(m_track);
}

bool AudioObject::isLooping() const {
    return m_track != nullptr && MIX_GetTrackLoops(m_track) != 0;
}

void AudioObject::setVolume(const NomadFloat volume) {
    const auto previousVolume = m_volume;
    m_volume = audio::clampVolume(volume);
    if (!applyVolume()) {
        m_volume = previousVolume;
    }
}

NomadFloat AudioObject::getVolume() const {
    return m_volume;
}

void AudioObject::setCategoryVolume(const NomadFloat volume) {
    const auto previousVolume = m_categoryVolume;
    m_categoryVolume = audio::clampVolume(volume);
    if (!applyVolume()) {
        m_categoryVolume = previousVolume;
    }
}

bool AudioObject::applyVolume() {
    if (!MIX_SetTrackGain(m_track, static_cast<float>(m_volume * m_categoryVolume))) {
        log::error(audio::getMixerError("Failed to set audio track volume"));
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
            log::error(audio::getMixerError("Failed to restart an audio track"));
            return;
        }
    }
    if (callback) {
        m_completionCallback = std::move(callback);
        m_completionArmed.store(true, std::memory_order_release);
    }
    if (!audio::playTrack(m_track, loops)) {
        cancelCompletion();
    }
}

void AudioObject::cancelCompletion() {
    m_completionArmed.store(false, std::memory_order_release);
    m_completionPending.store(false, std::memory_order_release);
    m_completionCallback = {};
}

} // namespace nomad
