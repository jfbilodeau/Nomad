// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Source.hpp>

#include <nomad/system/NomadConfiguration.hpp>

#include <filesystem>
#include <span>

namespace nomad {

[[nodiscard]] std::vector<NomadPath> getScriptPaths(const NomadConfiguration& configuration);

[[nodiscard]] std::vector<NomadPath> resolveScriptPaths(
    const NomadConfiguration& configuration,
    const NomadPath& projectDirectory
);

// Reads .nomad files or script roots. Each root defines its own relative script names.
// Directory traversal skips symbolic links and stops after ten directory levels.
// Loading failures are retained in the batch and prevent compilation.
// Overrides replace matching files before reading them. Unmatched overrides are added using their scriptName.
// File identities are absolute, lexically normalized paths (case-insensitive on Windows); untitled
// overrides use exact sourceName identity. Duplicate overrides are errors, not last-write-wins.
[[nodiscard]] SourceBatch loadSourcePaths(std::span<const std::filesystem::path> paths,
    std::span<const ScriptSource> overrides = {});

} // namespace nomad
