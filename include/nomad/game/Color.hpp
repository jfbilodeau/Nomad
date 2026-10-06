// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <SDL3/SDL_pixels.h>

#include <cstdint>

namespace nomad {

using Rgba = std::uint32_t;

class Color {
public:
    Color() = default;
    explicit Color(Rgba rgba);
    Color(Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha = 255);

    [[nodiscard]] Uint8 getRed() const;
    [[nodiscard]] Uint8 getGreen() const;
    [[nodiscard]] Uint8 getBlue() const;
    [[nodiscard]] Uint8 getAlpha() const;

    SDL_Color toSdlColor() const;
    void toSdlColor(SDL_Color& sdlColor) const;

    Rgba rgba = 0;

    [[nodiscard]] bool operator==(const Color& other) const = default;
};

namespace Colors {
    const auto Black = Color(0, 0, 0);
    const auto White = Color(255, 255, 255);
    const auto Red = Color(255, 0, 0);
    const auto Green = Color(0, 255, 0);
    const auto Blue = Color(0, 0, 255);
    const auto Yellow = Color(255, 255, 0);
    const auto Magenta = Color(255, 0, 255);
    const auto Cyan = Color(0, 255, 255);
    const auto Transparent = Color(0, 0, 0, 0);

    Color fromHexString(const NomadString& hexString);
} // Colors

} // nomad
