// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <utility>

#include <nomad/game/TileMap.hpp>

namespace nomad {

///////////////////////////////////////////////////////////////////////////////
// Tile
Tile::Tile(NomadInteger mask):
    m_mask(mask)
{}

///////////////////////////////////////////////////////////////////////////////
// TileSet
TileSet::TileSet(NomadIndex tileCount) {
    m_tiles.resize(tileCount);
}

TileSet::~TileSet() {

}

///////////////////////////////////////////////////////////////////////////////
// EntityDefinition
EntityDefinition::EntityDefinition(NomadId entityId, NomadString functionName):
    m_entityId(entityId),
    m_functionName(std::move(functionName))
{}

NomadId EntityDefinition::getEntityId() const {
    return m_entityId;
}

const NomadString& EntityDefinition::getFunctionName() const {
    return m_functionName;
}

///////////////////////////////////////////////////////////////////////////////
// EntitySet
EntitySet::EntitySet(NomadIndex entityCount) {
    m_entities.reserve(entityCount);
}

NomadId EntitySet::registerEntity(NomadId entityId, const NomadString& name) {
    auto id = toNomadId(m_entities.size());

    m_entities.emplace_back(
        entityId,
        name
    );

    return id;
}

const EntityDefinition* EntitySet::getEntity(NomadIndex index) const {
    return &m_entities[index];
}

///////////////////////////////////////////////////////////////////////////////
// TileLayer
TileLayer::TileLayer(NomadIndex width, NomadIndex height, TileSet* tileSet):
    m_width(width),
    m_height(height),
    m_tileSet(tileSet)
{
    m_tiles.resize(width * height);
}

NomadIndex TileLayer::getWidth() const {
    return m_width;
}

NomadIndex TileLayer::getHeight() const {
    return m_height;
}

const Tile& TileLayer::getTile(NomadIndex x, NomadIndex y) const {
    if (x < m_width && y < m_height) {
        return m_tiles[y * m_width + x];
    }

    // Return default 'void' tile
    if (!m_tiles.empty()) {
        return m_tiles[0];
    }

    static const Tile emptyTile;
    return emptyTile;
}

void TileLayer::setTile(NomadIndex x, NomadIndex y, const Tile& tile) {
    if (x >= m_width || y >= m_height) {
        return;
    }

    m_tiles[y * m_width + x] = tile;
}

const TileSet* TileLayer::getTileSet() const {
    return m_tileSet;
}

///////////////////////////////////////////////////////////////////////////////
// EntityInstance
EntityInstance::EntityInstance(
    NomadString entityId,
    NomadString  functionName,
    NomadInteger layer,
    const PointF& location
):
    m_entityId(std::move(entityId)),
    m_functionName(std::move(functionName)),
    m_layer(layer),
    m_location(location)
{
}

const NomadString& EntityInstance::getEntityId() const {
    return m_entityId;
}

const NomadString& EntityInstance::getFunctionName() const {
    return m_functionName;
}

const PointF& EntityInstance::getLocation() const {
    return m_location;
}

///////////////////////////////////////////////////////////////////////////////
// TileMap
TileMap::TileMap(NomadIndex width, NomadIndex height, NomadIndex layerCount):
    m_width(width),
    m_height(height)
{
    m_layers.reserve(layerCount);
}

NomadIndex TileMap::getWidth() const {
    return m_width;
}

NomadIndex TileMap::getHeight() const {
    return m_height;
}

NomadIndex TileMap::getLayerCount() const {
    return m_layers.size();
}

const TileLayer * TileMap::getLayer(NomadIndex index) const {
    return &m_layers[index];
}

} // namespace nomad
