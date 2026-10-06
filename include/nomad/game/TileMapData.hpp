// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/game/Color.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace nomad {

struct TileMapEntityData {
    NomadId id;
    NomadString functionName;
    NomadFloat x;
    NomadFloat y;
    NomadFloat width;
    NomadFloat height;
    NomadString textId;
};

struct TileMapLayerData {
    bool hasTileMap = false;
    std::vector<NomadIndex> tileIds;
    std::vector<TileMapEntityData> entities;
};

struct TileMapData {
    NomadInteger width;
    NomadInteger height;
    NomadInteger tileWidth;
    NomadInteger tileHeight;
    NomadIndex firstTileIndex;
    std::vector<NomadInteger> masks;
    std::vector<NomadString> tileNames;
    std::vector<TileMapLayerData> layers;
    std::optional<Color> backgroundColor;
};

// Parses and validates tile map content against the available tileset dimensions.
[[nodiscard]] TileMapData parseTileMapData(
    std::string_view content,
    NomadInteger textureWidth,
    NomadInteger textureHeight,
    NomadIndex layerCount
);

} // namespace nomad
