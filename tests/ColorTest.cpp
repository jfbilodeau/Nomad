// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include "nomad/game/Color.hpp"

using namespace nomad;

BOOST_AUTO_TEST_CASE(hex_color_parses_rgb_and_rgba_values)
{
    const auto rgbColor = Colors::fromHexString("#1234ab");
    BOOST_TEST(rgbColor.getRed() == 0x12);
    BOOST_TEST(rgbColor.getGreen() == 0x34);
    BOOST_TEST(rgbColor.getBlue() == 0xab);
    BOOST_TEST(rgbColor.getAlpha() == 0xff);

    const auto rgbaColor = Colors::fromHexString("1234ab80");
    BOOST_TEST(rgbaColor.getRed() == 0x12);
    BOOST_TEST(rgbaColor.getGreen() == 0x34);
    BOOST_TEST(rgbaColor.getBlue() == 0xab);
    BOOST_TEST(rgbaColor.getAlpha() == 0x80);

    const auto shortRgbColor = Colors::fromHexString("#f0c");
    BOOST_TEST(shortRgbColor.getRed() == 0xff);
    BOOST_TEST(shortRgbColor.getGreen() == 0x00);
    BOOST_TEST(shortRgbColor.getBlue() == 0xcc);
    BOOST_TEST(shortRgbColor.getAlpha() == 0xff);

    const auto shortRgbaColor = Colors::fromHexString("#fffc");
    BOOST_TEST(shortRgbaColor.getRed() == 0xff);
    BOOST_TEST(shortRgbaColor.getGreen() == 0xff);
    BOOST_TEST(shortRgbaColor.getBlue() == 0xff);
    BOOST_TEST(shortRgbaColor.getAlpha() == 0xcc);
}

BOOST_AUTO_TEST_CASE(hex_color_returns_default_for_invalid_input)
{
    BOOST_TEST(Colors::fromHexString("").rgba == Color{}.rgba);
    BOOST_TEST(Colors::fromHexString("#12").rgba == Color{}.rgba);
    BOOST_TEST(Colors::fromHexString("#12zzab").rgba == Color{}.rgba);
}
