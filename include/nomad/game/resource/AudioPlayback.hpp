// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <SDL3_mixer/SDL_mixer.h>

namespace nomad::audio {

[[nodiscard]] NomadFloat clampVolume(NomadFloat volume);
[[nodiscard]] NomadString getMixerError(NomadStringView operation);
[[nodiscard]] bool playTrack(MIX_Track* track, NomadInteger loops);

} // namespace nomad::audio
