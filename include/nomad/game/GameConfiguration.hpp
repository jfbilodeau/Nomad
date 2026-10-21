// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/game/GameOptions.hpp>

#include <nomad/system/NomadConfiguration.hpp>

namespace nomad {

[[nodiscard]] GameOptions readGameOptions(const NomadConfiguration& configuration);
[[nodiscard]] NomadPath resolveGameResourcePath(
    const NomadConfiguration& configuration,
    const NomadPath& projectDirectory
);
void normalizeGameConfiguration(const NomadConfiguration& source, NomadConfiguration& release);

} // namespace nomad
