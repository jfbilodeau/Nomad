// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Scene.hpp>

#include <nomad/game/Game.hpp>
#include <nomad/game/TileMapData.hpp>

#include <nomad/resource/ResourceManager.hpp>

#include <fstream>
#include <sstream>

namespace nomad {

// Load tile map
void Scene::loadTileMap(const NomadString& fileName, const NomadString& tileSetTextureName) {
    try {
        log::debug("Loading map '" + fileName + "' with tile texture: '" + tileSetTextureName + "'");

        // Get texture
        auto texture = m_game->getResources()->getTextures()->getTextureByName(tileSetTextureName);

        if (texture == nullptr) {
            log::error("Texture '" + tileSetTextureName + "' not found");
            return;
        }

        // Load tile map
        auto tileMapFileName = m_game->makeResourcePath(fileName);

        std::ifstream file(tileMapFileName);

        if (!file.is_open()) {
            log::error("Failed to open tile map file: " + tileMapFileName);
            return;
        }

        const auto content = NomadString(std::istreambuf_iterator<char>(file), {});
        auto tileMapData = parseTileMapData(
            content,
            texture->getWidth(),
            texture->getHeight(),
            SCENE_LAYER_COUNT
        );

        // Load complete. Initialize the scene.
        m_animatedTiles.clear();

        initTileSet(
            texture,
            tileMapData.tileWidth,
            tileMapData.tileHeight,
            tileMapData.firstTileIndex,
            tileMapData.masks
        );

        for (auto tileIndex = NomadIndex{0}; tileIndex < tileMapData.tileNames.size() && tileIndex < m_tiles.size(); ++tileIndex) {
            m_tiles[tileIndex].name = tileMapData.tileNames[tileIndex];
        }

        setTileMapSize(tileMapData.width, tileMapData.height);

        if (tileMapData.backgroundColor) {
            m_game->setClearColor(*tileMapData.backgroundColor);
        }

        while (!m_entities.empty()) {
            removeEntity(m_entities.back().get());
        }

        for (auto i = 0; i < SCENE_LAYER_COUNT; ++i) {
            auto& tileMapLayer = tileMapData.layers[i];
            m_layers[i].hasTileMap = tileMapLayer.hasTileMap;

            auto& layer = m_layers[i];
            layer.tileMap = std::move(tileMapLayer.tileIds);

            layer.entities.clear();

            for (auto& entity: tileMapLayer.entities) {
                auto functionId = m_game->getFunctionId(entity.functionName);

                if (functionId == NOMAD_INVALID_ID) {
                    log::error("Function '" + entity.functionName + "' for entity '" + std::to_string(entity.id) + "' not found");
                    continue;
                }

                // Lookup text
                NomadString text = NOMAD_EMPTY_STRING;

                if (entity.textId.empty() == false) {
                    m_game->getText(entity.textId, text);
                }

                m_game->addEntityToScene(
                    getId(),
                    functionId,
                    entity.x,
                    entity.y,
                    entity.width,
                    entity.height,
                    i,
                    text
                );
            }
        }

        log::debug("Map loaded");
    } catch (const std::exception& e) {
        log::error("Failed to load tile map: " + std::string(e.what()));
    }
}

} // nomad
