// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

namespace nomad {

enum class HorizontalAlignment {
    Left   = 0b00000001,
    Middle = 0b00000010,
    Right  = 0b00000100
};

enum class VerticalAlignment {
    Top    = 0b00010000,
    Center = 0b00100000,
    Bottom = 0b01000000
};

enum class Alignment {
    TopLeft   = 0b00010001,
    TopMiddle = 0b00010010,
    TopRight  = 0b00010100,

    CenterLeft   = 0b00100001,
    CenterMiddle = 0b00100010,
    CenterRight  = 0b00100100,

    BottomLeft   = 0b01000001,
    BottomMiddle = 0b01000010,
    BottomRight  = 0b01000100
};

HorizontalAlignment getHorizontalAlignment(Alignment alignment);
VerticalAlignment getVerticalAlignment(Alignment alignment);

Alignment getAlignment(HorizontalAlignment horizontal, VerticalAlignment vertical);

} // namespace nomad
