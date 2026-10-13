// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/geometry/Rectangle.hpp>

#include <nomad/resource/SpriteDiscovery.hpp>

#include <nomad/Nomad.hpp>

#include <memory>
#include <vector>

struct SDL_Surface;

namespace nomad {

class SpritePackerError final : public NomadException {
public:
    using NomadException::NomadException;
};

struct SdlSurfaceDeleter {
    void operator()(SDL_Surface* surface) const;
};

using SdlSurfacePtr = std::unique_ptr<SDL_Surface, SdlSurfaceDeleter>;

struct PackedSpriteDefinition {
    NomadString name;
    NomadIndex page;
    Rectangle source;
    Rectangle frame;
};

struct PackedSpritePage {
    NomadInteger width;
    NomadInteger height;
    SdlSurfacePtr surface;
};

struct PackedSpriteGroup {
    NomadString name;
    std::vector<PackedSpritePage> pages;
    std::vector<PackedSpriteDefinition> sprites;
};

[[nodiscard]] std::vector<PackedSpriteGroup> packSpriteGroups(
    const std::vector<SpriteSourceDefinition>& sources,
    NomadInteger maxTextureSize,
    NomadInteger padding
);

} // namespace nomad
