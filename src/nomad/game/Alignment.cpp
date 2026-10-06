// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Alignment.hpp>

namespace nomad {

HorizontalAlignment getHorizontalAlignment(Alignment alignment) {
    int bits = static_cast<int>(alignment);

    if (bits & 0b001) {
        return HorizontalAlignment::Left;
    } else if (bits & 0b010) {
        return HorizontalAlignment::Middle;
    } else if (bits & 0b100) {
        return HorizontalAlignment::Right;
    } else {
        return HorizontalAlignment::Middle;
    }
}

VerticalAlignment getVerticalAlignment(Alignment alignment) {
    int bits = static_cast<int>(alignment);

    if (bits & 0b0001000) {
        return VerticalAlignment::Top;
    } else if (bits & 0b0010000) {
        return VerticalAlignment::Center;
    } else if (bits & 0b0100000) {
        return VerticalAlignment::Bottom;
    } else {
        return VerticalAlignment::Center;
    }
}

Alignment getAlignment(HorizontalAlignment horizontal, VerticalAlignment vertical) {
    int bits = 0;

    switch (horizontal) {
        case HorizontalAlignment::Left:
            bits |= 0b001;
            break;
        case HorizontalAlignment::Middle:
            bits |= 0b010;
            break;
        case HorizontalAlignment::Right:
            bits |= 0b100;
            break;
    }

    switch (vertical) {
        case VerticalAlignment::Top:
            bits |= 0b0001000;
            break;
        case VerticalAlignment::Center:
            bits |= 0b0010000;
            break;
        case VerticalAlignment::Bottom:
            bits |= 0b0100000;
            break;
    }

    return static_cast<Alignment>(bits);
}


} // namespace nomad
