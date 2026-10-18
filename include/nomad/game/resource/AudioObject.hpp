// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>

#include <SDL3_mixer/SDL_mixer.h>

#include <atomic>
#include <functional>

namespace nomad {

class AudioManager;
class AudioResource;

class AudioObject final : public NomadObject {
public:
    using CompletionCallback = std::function<void()>;

    AudioObject(const AudioResource* resource, MIX_Mixer* mixer, NomadFloat categoryVolume);
    AudioObject(const AudioObject&) = delete;
    AudioObject& operator=(const AudioObject&) = delete;
    ~AudioObject() override;

    [[nodiscard]] const AudioResource* getResource() const;

    void playOnce();
    void playOnceThen(CompletionCallback callback);
    void playLoop();
    void pause();
    void resume();
    void stop();
    void pauseAutomatically();
    void resumeAutomatically();
    [[nodiscard]] CompletionCallback takeCompletionCallback();

    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] bool isPaused() const;
    [[nodiscard]] bool isLooping() const;

    void setVolume(NomadFloat volume);
    [[nodiscard]] NomadFloat getVolume() const;
    void setCategoryVolume(NomadFloat volume);

private:
    friend class AudioManager;

    void detachManager();
    static void SDLCALL trackStoppedCallback(void* userdata, MIX_Track* track);
    void play(NomadInteger loops, CompletionCallback callback = {});
    void cancelCompletion();
    [[nodiscard]] bool applyVolume();

    AudioManager* m_manager = nullptr;
    const AudioResource* m_resource;
    MIX_Track* m_track;
    NomadFloat m_volume = 1.0;
    NomadFloat m_categoryVolume;
    bool m_automaticallyPaused = false;
    std::atomic_bool m_completionArmed = false;
    std::atomic_bool m_completionPending = false;
    CompletionCallback m_completionCallback;
};

} // namespace nomad
