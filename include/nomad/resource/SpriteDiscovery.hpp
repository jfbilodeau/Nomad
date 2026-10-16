// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

class SpriteDiscoveryException final : public NomadException {
public:
    using NomadException::NomadException;
};

struct SpriteSourceDefinition {
    NomadPath filePath;
    NomadString name;
    NomadString group;
};

[[nodiscard]] std::vector<SpriteSourceDefinition> discoverSpriteSources(
    const NomadPath& resourcePath,
    const std::vector<NomadPath>& directories,
    bool directoriesExplicitlyConfigured
);

} // namespace nomad
