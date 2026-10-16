// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/resource/SpritePacker.hpp>

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

inline constexpr NomadInteger NOMAD_SPRITE_CATALOG_SCHEMA_VERSION = 1;
inline constexpr NomadStringView NOMAD_SPRITE_CATALOG_FILE_NAME = "catalog.json";

class SpriteCatalogException final : public NomadException {
public:
    using NomadException::NomadException;
};

[[nodiscard]] NomadString makePackedSpritePageFileName(
    const NomadString& group,
    NomadIndex page
);

[[nodiscard]] bool hasSpriteCatalog(const NomadPath& spriteRoot);

[[nodiscard]] std::vector<PackedSpriteGroup> loadSpriteCatalog(
    const NomadPath& spriteRoot
);

void writeSpriteCatalog(
    const NomadPath& spriteRoot,
    const std::vector<PackedSpriteGroup>& groups
);

[[nodiscard]] std::vector<PackedSpriteGroup> loadSpriteRoot(
    const NomadPath& resourcePath,
    const NomadPath& directory,
    bool directoryRequired,
    NomadInteger maxTextureSize,
    NomadInteger padding
);

} // namespace nomad
