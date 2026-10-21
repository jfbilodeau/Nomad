// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <span>
#include <vector>

namespace nomad {

[[nodiscard]] std::vector<NomadPath> normalizeRelativeDirectories(
    std::span<const NomadString> directories
);

[[nodiscard]] inline NomadPath pathFromUtf8(const NomadStringView path) {
    std::u8string encoded;
    encoded.reserve(path.size());

    for (const auto character : path) {
        encoded.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }

    return NomadPath(encoded);
}

[[nodiscard]] inline NomadString pathToUtf8(const NomadPath& path) {
    const auto encoded = path.u8string();

    return NomadString(reinterpret_cast<const NomadChar*>(encoded.data()), encoded.size());
}

[[nodiscard]] inline NomadString pathToGenericUtf8(const NomadPath& path) {
    const auto encoded = path.generic_u8string();

    return NomadString(reinterpret_cast<const NomadChar*>(encoded.data()), encoded.size());
}

} // namespace nomad
