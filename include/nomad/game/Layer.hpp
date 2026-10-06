// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#ifndef NOMAD_LAYER_HPP
#define NOMAD_LAYER_HPP

#include <nomad/game/Entity.hpp>
#include <nomad/game/TileMap.hpp>

namespace nomad {

class Layer {
public:
    Layer();

    void addEntity(Entity* entity);
    void removeEntity(const Entity* entity);

    void resizeTileMap(NomadInteger width, NomadInteger height);
    [[nodiscard]] NomadInteger getTileLayerWidth() const;
    [[nodiscard]] NomadInteger getTileLayerHeight() const;
    void setTile(NomadInteger x, NomadInteger y, NomadIndex tileId);
    [[nodiscard]] NomadIndex getTile(NomadInteger x, NomadInteger y) const;

private:
    NomadInteger m_tileLayerWidth = 0;
    NomadInteger m_tileLayerHeight = 0;
    std::vector<NomadIndex> m_tileLayer;
    std::vector<Entity*> m_entities;
};

} // namespace nomad

#endif //NOMAD_LAYER_HPP
