// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/Path.hpp>

#include <nomad/system/String.hpp>

#include <algorithm>
#include <utility>

namespace nomad {

std::vector<NomadPath> normalizeRelativeDirectories(
    const std::span<const NomadString> directories
) {
    std::vector<NomadPath> result;
    std::vector<NomadString> normalizedKeys;
    for (auto value : directories) {
        std::ranges::replace(value, '\\', '/');
        auto path = pathFromUtf8(value).lexically_normal();
        if (!path.empty() && !path.has_filename()) {
            path = path.parent_path();
        }
        if (path.empty() || path.has_root_path() || path == "." || *path.begin() == "..") {
            throw NomadException("must contain only relative directories below the base directory");
        }
        const auto key = stringToLowerAscii(pathToGenericUtf8(path));
        for (const auto& existing : normalizedKeys) {
            if (existing == key ||
                (existing.size() < key.size() && key.starts_with(existing) && key[existing.size()] == '/') ||
                (key.size() < existing.size() && existing.starts_with(key) && existing[key.size()] == '/')) {
                throw NomadException("contains duplicate or overlapping directories");
            }
        }
        normalizedKeys.push_back(key);
        result.push_back(std::move(path));
    }
    return result;
}

} // namespace nomad
