// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/geometry/Rectangle.hpp>

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

class RectanglePackerException final : public NomadException {
public:
    using NomadException::NomadException;
};

struct RectanglePackingInput {
    NomadIndex id;
    NomadInteger width;
    NomadInteger height;
};

struct PackedRectangle {
    NomadIndex id;
    Rectangle frame;
};

struct RectanglePackingPage {
    NomadInteger width = 0;
    NomadInteger height = 0;
    std::vector<PackedRectangle> rectangles;
};

[[nodiscard]] std::vector<RectanglePackingPage> packRectangles(
    const std::vector<RectanglePackingInput>& inputs,
    NomadInteger maxTextureSize,
    NomadInteger padding
);

} // namespace nomad
