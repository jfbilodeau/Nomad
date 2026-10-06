// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Scene.hpp>

#include <nomad/debug/DebugDraw.hpp>

#include <nomad/game/Canvas.hpp>
#include <nomad/game/Game.hpp>

namespace nomad {

// Render scene
void Scene::render(Canvas* canvas) const {
    const auto previousOffset = canvas->getOffset();

    const auto resolution = m_game->getResolution();

    const auto resolutionWidth = static_cast<NomadFloat>(resolution.getX());
    const auto resolutionHeight = static_cast<NomadFloat>(resolution.getY());

    // Use integer-aligned camera offset to avoid sub-pixel gaps between tiles
    const auto rawOffsetX = -m_cameraPosition.getX() + resolutionWidth / 2.0f;
    const auto rawOffsetY = -m_cameraPosition.getY() + resolutionHeight / 2.0f;

    const auto alignedOffsetX = std::round(rawOffsetX);
    const auto alignedOffsetY = std::round(rawOffsetY);

    canvas->setOffset(alignedOffsetX, alignedOffsetY);

    for (const auto& layer : m_layers) {
        // Render tile map
        if (m_tileTexture != nullptr) {
            renderTileMap(canvas, layer);
        }
        // Render entities
        for (const auto entity : layer.entities) {
            entity->render(canvas);
        }
    }

    if (m_game->isDebug()) {
        b2DebugDraw debug_draw;
        createDebugDraw(canvas, &debug_draw);

        for (const auto& layer : m_layers) {
            b2World_Draw(layer.worldId, &debug_draw);
        }
    }

    // Render tint.
    if (m_tint.getAlpha() != 0x00) {
        auto width = static_cast<NomadFloat>(resolution.getX());
        auto height = static_cast<NomadFloat>(resolution.getY());

        const RectangleF tintRect(
            -m_cameraPosition.getX() + width / 2.0f,
            -m_cameraPosition.getY() + height / 2.0f,
            width,
            height
        );

        canvas->renderRectangle(tintRect, m_tint);
    }

    canvas->setOffset(previousOffset);
}

void Scene::renderTileMap(const Canvas* canvas, const Scene::Layer& layer) const {
    if (layer.hasTileMap) {
        for (auto y = 0; y < m_tileMapHeight; ++y) {
            for (auto x = 0; x < m_tileMapWidth; ++x) {
                const auto groundTileIndex = layer.tileMap[y * m_tileMapWidth + x];
                if (groundTileIndex > 0) {
                    renderTile(canvas, static_cast<Coord>(y), static_cast<Coord>(x), groundTileIndex);
                }
            }
        }
    }
}

void Scene::renderTile(const Canvas* canvas, const Coord y, const Coord x, const NomadIndex tileIndex) const {
    auto displayedTileIndex = tileIndex;
    for (const auto& animatedTile : m_animatedTiles) {
        if (tileIndex == animatedTile.startId) {
            displayedTileIndex = animatedTile.resolveTileId(tileIndex, m_frameNumber);
            break;
        }
    }

    if (displayedTileIndex > 0 && displayedTileIndex < m_tiles.size()) {
        const auto source = m_tiles[displayedTileIndex].source.toRectangleF();

        const auto tileWidth = static_cast<Coord>(m_tileWidth);
        const auto tileHeight = static_cast<Coord>(m_tileHeight);

        const RectangleF destination(
            x * tileWidth,
            y * tileHeight,
            tileWidth,
            tileHeight
        );

        canvas->renderTexture(m_tileTexture, source, destination);
    }
}

} // nomad
