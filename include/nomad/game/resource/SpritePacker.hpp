// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/geometry/Rectangle.hpp>

#include <nomad/game/resource/SpriteDiscovery.hpp>

#include <nomad/Nomad.hpp>

#include <memory>
#include <vector>

struct SDL_Surface;

namespace nomad {

class SpritePackerException final : public NomadException {
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

struct SpritePackingPageReport {
    NomadString name;
    NomadInteger width;
    NomadInteger height;
    NomadIndex spriteCount;
    NomadIndex uniqueSpriteCount;
    NomadInteger usedPixelCount;
};

struct SpritePackingReport {
    NomadIndex sourceCount;
    NomadIndex groupCount;
    NomadIndex pageCount;
    NomadIndex uniqueSpriteCount;
    std::vector<SpritePackingPageReport> pages;
};

[[nodiscard]] NomadString makePackedSpritePageName(const NomadString& group, NomadIndex page);

[[nodiscard]] std::vector<PackedSpriteGroup> packSpriteGroups(
    const std::vector<SpriteSourceDefinition>& sources,
    NomadInteger maxTextureSize,
    NomadInteger padding
);

[[nodiscard]] SpritePackingReport analyzeSpritePacking(
    const NomadPath& resourcePath,
    const std::vector<NomadPath>& directories,
    bool directoriesExplicitlyConfigured,
    NomadInteger maxTextureSize,
    NomadInteger padding,
    bool missingDirectoriesAllowed = false
);

} // namespace nomad
