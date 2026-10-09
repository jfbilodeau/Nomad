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

    if (bits & static_cast<int>(VerticalAlignment::Top)) {
        return VerticalAlignment::Top;
    } else if (bits & static_cast<int>(VerticalAlignment::Center)) {
        return VerticalAlignment::Center;
    } else if (bits & static_cast<int>(VerticalAlignment::Bottom)) {
        return VerticalAlignment::Bottom;
    } else {
        return VerticalAlignment::Center;
    }
}

Alignment getAlignment(HorizontalAlignment horizontal, VerticalAlignment vertical) {
    int bits = 0;

    switch (horizontal) {
        case HorizontalAlignment::Left:
            bits |= static_cast<int>(HorizontalAlignment::Left);
            break;
        case HorizontalAlignment::Middle:
            bits |= static_cast<int>(HorizontalAlignment::Middle);
            break;
        case HorizontalAlignment::Right:
            bits |= static_cast<int>(HorizontalAlignment::Right);
            break;
    }

    switch (vertical) {
        case VerticalAlignment::Top:
            bits |= static_cast<int>(VerticalAlignment::Top);
            break;
        case VerticalAlignment::Center:
            bits |= static_cast<int>(VerticalAlignment::Center);
            break;
        case VerticalAlignment::Bottom:
            bits |= static_cast<int>(VerticalAlignment::Bottom);
            break;
    }

    return static_cast<Alignment>(bits);
}


} // namespace nomad
