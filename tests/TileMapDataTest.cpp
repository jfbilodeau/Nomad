// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include "nomad/game/TileMapData.hpp"

using namespace nomad;

namespace {

constexpr auto validTileMap = R"({
    "width": 2,
    "height": 1,
    "tilewidth": 16,
    "tileheight": 16,
    "tilesets": [{
        "firstgid": 1,
        "tilecount": 4,
        "tiles": [{
            "id": 2,
            "properties": [{"name": "name", "type": "string", "value": "conveyor.east"}]
        }]
    }],
    "layers": [{"name": "layer-0-tiles", "data": [1, 2]}]
})";

} // namespace

BOOST_AUTO_TEST_CASE(tile_map_data_parses_valid_tile_layers)
{
    const auto data = parseTileMapData(validTileMap, 32, 32, 2);

    BOOST_TEST(data.width == 2);
    BOOST_TEST(data.height == 1);
    BOOST_TEST(data.tileWidth == 16);
    BOOST_TEST(data.tileHeight == 16);
    BOOST_TEST(data.tileNames.size() == 5U);
    BOOST_TEST(data.tileNames[3] == "conveyor.east");
    BOOST_TEST(data.tileNames[1].empty());
    BOOST_TEST(data.layers.size() == 2U);
    BOOST_TEST(data.layers[0].hasTileMap);
    BOOST_TEST(data.layers[0].tileIds[0] == 1U);
    BOOST_TEST(data.layers[0].tileIds[1] == 2U);
    BOOST_TEST(!data.layers[1].hasTileMap);
}

BOOST_AUTO_TEST_CASE(tile_map_data_rejects_invalid_dimensions)
{
    constexpr auto invalidDimensions = R"({
        "width": 0,
        "height": 1,
        "tilewidth": 16,
        "tileheight": 16,
        "tilesets": [{"firstgid": 1, "tilecount": 4, "tiles": []}],
        "layers": []
    })";

    BOOST_CHECK_THROW(
        [&] { static_cast<void>(parseTileMapData(invalidDimensions, 32, 32, 1)); }(),
        std::runtime_error
    );
}

BOOST_AUTO_TEST_CASE(tile_map_data_rejects_short_tile_layers)
{
    constexpr auto shortLayer = R"({
        "width": 2,
        "height": 1,
        "tilewidth": 16,
        "tileheight": 16,
        "tilesets": [{"firstgid": 1, "tilecount": 4, "tiles": []}],
        "layers": [{"name": "layer-0-tiles", "data": [1]}]
    })";

    BOOST_CHECK_THROW(
        [&] { static_cast<void>(parseTileMapData(shortLayer, 32, 32, 1)); }(),
        std::runtime_error
    );
}
