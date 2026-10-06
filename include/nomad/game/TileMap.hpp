// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/geometry/PointF.hpp>

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

///////////////////////////////////////////////////////////////////////////////
// Tile
///////////////////////////////////////////////////////////////////////////////
class Tile {
public:
    Tile() = default;
    explicit Tile(NomadInteger mask);

private:
    NomadInteger m_mask = 0;
};

///////////////////////////////////////////////////////////////////////////////
// TileSet
///////////////////////////////////////////////////////////////////////////////
class TileSet {
public:
    explicit TileSet(NomadIndex tileCount);
    ~TileSet();

private:
    std::vector<Tile> m_tiles;
};

///////////////////////////////////////////////////////////////////////////////
// EntityDefinition
///////////////////////////////////////////////////////////////////////////////
class EntityDefinition {
public:
    explicit EntityDefinition(NomadId entityId, NomadString functionName);

    [[nodiscard]] NomadId getEntityId() const;
    [[nodiscard]] const NomadString& getFunctionName() const;

private:
    NomadId m_entityId;
    NomadString m_functionName;
};

///////////////////////////////////////////////////////////////////////////////
// EntitySet
///////////////////////////////////////////////////////////////////////////////
class EntitySet {
public:
    explicit EntitySet(NomadIndex entityCount = 0);

    NomadId registerEntity(NomadId entityId, const NomadString& name);

    [[nodiscard]] const EntityDefinition* getEntity(NomadIndex index) const;

private:
    std::vector<EntityDefinition> m_entities;
};

///////////////////////////////////////////////////////////////////////////////
// TileLayer
///////////////////////////////////////////////////////////////////////////////
class TileLayer {
public:
    explicit TileLayer(NomadIndex width = 0, NomadIndex height = 0, TileSet* tileSet = nullptr);

    [[nodiscard]] NomadIndex getWidth() const;
    [[nodiscard]] NomadIndex getHeight() const;

    [[nodiscard]] const Tile& getTile(NomadIndex x, NomadIndex y) const;
    void setTile(NomadIndex x, NomadIndex y, const Tile& tile);

    [[nodiscard]] const TileSet* getTileSet() const;

private:
    NomadIndex m_width;
    NomadIndex m_height;
    TileSet* m_tileSet;
    std::vector<Tile> m_tiles;
};

///////////////////////////////////////////////////////////////////////////////
// EntityInstance
///////////////////////////////////////////////////////////////////////////////
class EntityInstance {
public:
    EntityInstance(NomadString entityId, NomadString  functionName, NomadInteger layer, const PointF& location);

    [[nodiscard]] const NomadString& getEntityId() const;
    [[nodiscard]] const NomadString& getFunctionName() const;
    [[nodiscard]] const PointF& getLocation() const;

private:
    NomadString m_entityId;
    NomadString m_functionName;
    NomadInteger m_layer;
    PointF m_location;
};

///////////////////////////////////////////////////////////////////////////////
// TileMap
///////////////////////////////////////////////////////////////////////////////
class TileMap {
public:
    TileMap(NomadIndex width, NomadIndex height, NomadIndex layerCount);

    [[nodiscard]] NomadIndex getWidth() const;
    [[nodiscard]] NomadIndex getHeight() const;
    [[nodiscard]] NomadIndex getLayerCount() const;

    [[nodiscard]] const TileLayer* getLayer(NomadIndex index) const;

private:
    NomadIndex m_width, m_height;

    std::vector<TileLayer> m_layers;
};

} // namespace nomad
