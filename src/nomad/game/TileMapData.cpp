// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/TileMapData.hpp>

#include <boost/json.hpp>

#include <limits>
#include <stdexcept>

namespace nomad {

namespace {

NomadIndex getCellCount(const NomadInteger width, const NomadInteger height) {
    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Tile map dimensions must be positive");
    }

    const auto mapWidth = static_cast<NomadIndex>(width);
    const auto mapHeight = static_cast<NomadIndex>(height);

    if (mapWidth > std::numeric_limits<NomadIndex>::max() / mapHeight) {
        throw std::runtime_error("Tile map dimensions are too large");
    }

    return mapWidth * mapHeight;
}

NomadIndex loadTileSetMasks(
    const boost::json::array& tilesets,
    std::vector<NomadInteger>& masks,
    std::vector<NomadString>& tileNames
) {
    for (const auto& tileset : tilesets) {
        if (tileset.try_at("objectalignment")) {
            continue;
        }

        const auto firstTileId = tileset.at("firstgid").as_int64();
        const auto tileCount = tileset.at("tilecount").as_int64();
        if (firstTileId < 0 || tileCount < 0) {
            throw std::runtime_error("Tileset IDs and counts must be non-negative");
        }

        const auto firstTileIndex = static_cast<NomadIndex>(firstTileId);
        const auto tileCountIndex = static_cast<NomadIndex>(tileCount);
        if (firstTileIndex > std::numeric_limits<NomadIndex>::max() - tileCountIndex) {
            throw std::runtime_error("Tileset size is too large");
        }

        masks.resize(firstTileIndex + tileCountIndex);
        tileNames.resize(firstTileIndex + tileCountIndex);

        for (const auto& tile : tileset.at("tiles").as_array()) {
            const auto tileId = tile.at("id").as_int64();
            if (tileId < 0 || static_cast<NomadIndex>(tileId) >= tileCountIndex) {
                throw std::runtime_error("Tileset tile ID is out of range");
            }
            const auto tileIndex = firstTileIndex + static_cast<NomadIndex>(tileId);

            if (const auto properties = tile.try_at("properties")) {
                for (const auto& property : properties->as_array()) {
                    const auto propertyName = property.at("name").as_string();
                    if (propertyName == "mask") {
                        masks[tileIndex] = property.at("value").as_int64();
                    } else if (propertyName == "name") {
                        tileNames[tileIndex] = property.at("value").as_string();
                    }
                }
            }
        }

        return firstTileIndex;
    }

    throw std::runtime_error("No tileset found in tile map");
}

bool loadLayer(
    const boost::json::array& layers,
    const NomadString& layerName,
    const NomadIndex width,
    const NomadIndex height,
    const NomadIndex tileCount,
    TileMapLayerData& layerData
) {
    for (const auto& layer : layers) {
        if (layer.at("name").as_string() != layerName) {
            continue;
        }

        const auto data = layer.at("data").as_array();
        if (data.size() != layerData.tileIds.size()) {
            throw std::runtime_error("Tile layer '" + layerName + "' has an invalid cell count");
        }

        for (auto y = NomadIndex{0}; y < height; ++y) {
            for (auto x = NomadIndex{0}; x < width; ++x) {
                const auto index = y * width + x;
                const auto tileId = data[index].as_int64();
                layerData.tileIds[index] = tileId >= 0 && static_cast<NomadIndex>(tileId) < tileCount
                    ? static_cast<NomadIndex>(tileId)
                    : NomadIndex{0};
            }
        }

        return true;
    }

    return false;
}

void loadEntities(const boost::json::array& layers, const NomadString& layerName, TileMapLayerData& layerData) {
    for (const auto& layer : layers) {
        if (layer.at("name").as_string() != layerName) {
            continue;
        }

        for (const auto& object : layer.at("objects").as_array()) {
            TileMapEntityData entity{
                toNomadId(object.at("id").as_int64()),
                {},
                object.at("x").to_number<NomadFloat>(),
                object.at("y").to_number<NomadFloat>(),
                object.at("width").to_number<NomadFloat>(),
                object.at("height").to_number<NomadFloat>(),
                {}
            };

            for (const auto& property : object.at("properties").as_array()) {
                const auto name = property.at("name").as_string();
                const auto value = property.at("value").as_string();
                // Tiled map files store the entity's initialization function under the
                // `script` property. Resource-level naming stays "script".
                if (name == "script") {
                    entity.functionName = value;
                } else if (name == "text") {
                    entity.textId = value;
                }
            }

            if (entity.functionName.empty()) {
                continue;
            }

            layerData.entities.push_back(std::move(entity));
        }

        return;
    }
}

} // namespace

TileMapData parseTileMapData(
    const std::string_view content,
    const NomadInteger textureWidth,
    const NomadInteger textureHeight,
    const NomadIndex layerCount
) {
    const auto json = boost::json::parse(content).as_object();
    const auto width = json.at("width").as_int64();
    const auto height = json.at("height").as_int64();
    const auto tileWidth = json.at("tilewidth").as_int64();
    const auto tileHeight = json.at("tileheight").as_int64();
    const auto cellCount = getCellCount(width, height);

    if (tileWidth <= 0 || tileHeight <= 0) {
        throw std::runtime_error("Tile dimensions must be positive");
    }

    const auto horizontalTileCount = textureWidth / tileWidth;
    const auto verticalTileCount = textureHeight / tileHeight;
    if (horizontalTileCount <= 0 || verticalTileCount <= 0) {
        throw std::runtime_error("Tileset texture is smaller than one tile");
    }

    TileMapData result{
        width,
        height,
        tileWidth,
        tileHeight,
        0,
        {},
        {},
        std::vector<TileMapLayerData>(layerCount),
        std::nullopt
    };

    if (const auto backgroundColor = json.try_at("backgroundcolor")) {
        result.backgroundColor = Colors::fromHexString(backgroundColor->as_string().c_str());
    }

    result.firstTileIndex = loadTileSetMasks(json.at("tilesets").as_array(), result.masks, result.tileNames);
    const auto availableTileCount = static_cast<NomadIndex>(horizontalTileCount) * static_cast<NomadIndex>(verticalTileCount) + result.firstTileIndex;
    if (result.masks.size() > availableTileCount) {
        throw std::runtime_error("Tileset declares more tiles than the texture contains");
    }

    const auto layers = json.at("layers").as_array();
    for (auto layerIndex = NomadIndex{0}; layerIndex < layerCount; ++layerIndex) {
        auto& layerData = result.layers[layerIndex];
        layerData.tileIds.resize(cellCount);

        const auto layerName = "layer-" + std::to_string(layerIndex);
        layerData.hasTileMap = loadLayer(
            layers,
            layerName + "-tiles",
            static_cast<NomadIndex>(height),
            static_cast<NomadIndex>(width),
            availableTileCount,
            layerData
        );
        loadEntities(layers, layerName + "-entities", layerData);
    }

    return result;
}

} // namespace nomad
