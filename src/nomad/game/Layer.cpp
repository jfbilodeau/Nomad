// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Layer.hpp>

#include <ranges>

namespace nomad {

Layer::Layer():
    m_tileLayer()
{
}

void Layer::addEntity(Entity* entity) {
    m_entities.push_back(entity);
}

void Layer::removeEntity(const Entity* entity) {
    const auto it = std::ranges::find(m_entities, entity);

    if (it != m_entities.end()) {
        m_entities.erase(it);
    }
}

void Layer::resizeTileMap(const NomadInteger width, const NomadInteger height) {
    // Create new tile layer. Copy old tiles to new layer and move new layer to m_tile_layer.
    std::vector<NomadIndex> newTileLayer(width * height);

    for (NomadInteger y = 0; y < height; ++y) {
        for (NomadInteger x = 0; x < width; ++x) {
            if (x < m_tileLayerWidth && y < m_tileLayerHeight) {
                newTileLayer[y * width + x] = m_tileLayer[y * m_tileLayerWidth + x];
            } else {
                newTileLayer[y * width + x] = 0;
            }
        }
    }

    m_tileLayer = std::move(newTileLayer);

    m_tileLayerWidth = width;
    m_tileLayerHeight = height;
}

NomadInteger Layer::getTileLayerWidth() const {
    return m_tileLayerWidth;
}

NomadInteger Layer::getTileLayerHeight() const {
    return m_tileLayerHeight;
}

void Layer::setTile(const NomadInteger x, const NomadInteger y, const NomadIndex tileId) {
    if (x >= m_tileLayerWidth || y >= m_tileLayerHeight) {
        return;
    }

    m_tileLayer[y * m_tileLayerWidth + x] = tileId;
}

NomadIndex Layer::getTile(const NomadInteger x, const NomadInteger y) const {
    if (x >= m_tileLayerWidth || y >= m_tileLayerHeight) {
        return 0;
    }

    return m_tileLayer[y * m_tileLayerWidth + x];
}

} // namespace nomad
