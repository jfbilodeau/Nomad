// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Cardinal.hpp>

namespace nomad {

const NomadString& to_string(Cardinal cardinal) {
    switch (cardinal) {
        case Cardinal::North:
            return Cardinals::North;
        case Cardinal::South:
            return Cardinals::South;
        case Cardinal::East:
            return Cardinals::East;
        case Cardinal::West:
            return Cardinals::West;

        default:
            return Cardinals::Unknown;
    }
}

Cardinal from_string(const NomadString& cardinal_name) {
    if (cardinal_name == Cardinals::North) {
        return Cardinal::North;
    } else if (cardinal_name == Cardinals::South) {
        return Cardinal::South;
    } else if (cardinal_name == Cardinals::East) {
        return Cardinal::East;
    } else if (cardinal_name == Cardinals::West) {
        return Cardinal::West;
    }

    return Cardinal::Unknown;
}

} // namespace nomad
