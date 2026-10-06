// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include "nomad/game/TileMap.hpp"

using namespace nomad;

BOOST_AUTO_TEST_CASE(tile_layer_returns_requested_tile)
{
    const TileLayer layer(2, 2);

    BOOST_TEST(&layer.getTile(0, 0) != &layer.getTile(1, 0));
    BOOST_TEST(&layer.getTile(0, 0) != &layer.getTile(0, 1));
    BOOST_TEST(&layer.getTile(1, 0) != &layer.getTile(1, 1));
}

BOOST_AUTO_TEST_CASE(tile_layer_returns_void_tile_for_invalid_coordinates)
{
    const TileLayer layer(2, 2);

    const auto* horizontalOverflow = &layer.getTile(2, 0);
    const auto* verticalOverflow = &layer.getTile(0, 2);

    BOOST_TEST(horizontalOverflow == verticalOverflow);
    BOOST_TEST(horizontalOverflow == &layer.getTile(0, 0));
}

BOOST_AUTO_TEST_CASE(empty_tile_layer_returns_void_tile)
{
    const TileLayer emptyLayer;

    BOOST_TEST(&emptyLayer.getTile(0, 0) == &emptyLayer.getTile(1, 0));
}
