// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

class AudioDiscoveryException final : public NomadException {
public:
    using NomadException::NomadException;
};

struct AudioSourceDefinition {
    NomadPath filePath;
    NomadString name;
};

[[nodiscard]] std::vector<AudioSourceDefinition> discoverAudioSources(
    const NomadPath& resourcePath,
    const std::vector<NomadPath>& directories,
    bool directoriesExplicitlyConfigured
);

} // namespace nomad
