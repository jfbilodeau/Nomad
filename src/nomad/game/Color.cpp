// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Color.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/system/TempHeap.hpp>

namespace nomad {

namespace {

int getHexDigitValue(const char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }

    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }

    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }

    return -1;
}

bool parseHexByte(const NomadChar highCharacter, const NomadChar lowCharacter, Uint8& value) {
    const auto highValue = getHexDigitValue(highCharacter);
    const auto lowValue = getHexDigitValue(lowCharacter);

    if (highValue < 0 || lowValue < 0) {
        return false;
    }

    value = static_cast<Uint8>((highValue << 4) | lowValue);
    return true;
}

} // namespace

Color::Color(const Rgba rgba) :
    rgba(rgba)
{
}

Color::Color(const Uint8 red, const Uint8 green, const Uint8 blue, const Uint8 alpha):
    rgba(
        (static_cast<Rgba>(red) << 24) |
        (static_cast<Rgba>(green) << 16) |
        (static_cast<Rgba>(blue) << 8) |
        static_cast<Rgba>(alpha)
    )
{
}

Uint8 Color::getRed() const {
    return static_cast<Uint8>(rgba >> 24);
}

Uint8 Color::getGreen() const {
    return static_cast<Uint8>(rgba >> 16);
}

Uint8 Color::getBlue() const {
    return static_cast<Uint8>(rgba >> 8);
}

Uint8 Color::getAlpha() const {
    return static_cast<Uint8>(rgba);
}

SDL_Color Color::toSdlColor() const {
    return {
        .r = getRed(),
        .g = getGreen(),
        .b = getBlue(),
        .a = getAlpha()
    };
}

void Color::toSdlColor(SDL_Color& sdlColor) const {
    sdlColor.r = getRed();
    sdlColor.g = getGreen();
    sdlColor.b = getBlue();
    sdlColor.a = getAlpha();
}

Color Colors::fromHexString(const NomadString &hexString) {
    auto hex = createTempString(hexString);

    if (!hex.empty() && hex[0] == '#') {
        hex = hex.substr(1);
    }

    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    Uint8 a = 255;
    auto valid = false;

    switch (hex.length()) {
        case 3:
            valid = parseHexByte(hex[0], hex[0], r) &&
                parseHexByte(hex[1], hex[1], g) &&
                parseHexByte(hex[2], hex[2], b);
            break;
        case 4:
            valid = parseHexByte(hex[0], hex[0], r) &&
                parseHexByte(hex[1], hex[1], g) &&
                parseHexByte(hex[2], hex[2], b) &&
                parseHexByte(hex[3], hex[3], a);
            break;
        case 6:
            valid = parseHexByte(hex[0], hex[1], r) &&
                parseHexByte(hex[2], hex[3], g) &&
                parseHexByte(hex[4], hex[5], b);
            break;
        case 8:
            valid = parseHexByte(hex[0], hex[1], r) &&
                parseHexByte(hex[2], hex[3], g) &&
                parseHexByte(hex[4], hex[5], b) &&
                parseHexByte(hex[6], hex[7], a);
            break;
        default:
            break;
    }

    if (!valid) {
        log::warning(NomadString("Invalid hex color string: ") + hex.c_str());
        return {};
    }

    return { r, g, b, a };
}

} // nomad
