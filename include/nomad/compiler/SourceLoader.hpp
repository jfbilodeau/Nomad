// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Source.hpp>

#include <nomad/system/ModuleManifest.hpp>
#include <nomad/system/NomadConfiguration.hpp>

#include <filesystem>
#include <span>

namespace nomad {

[[nodiscard]] std::vector<NomadPath> getScriptPaths(const NomadConfiguration& configuration);

[[nodiscard]] std::vector<NomadPath> resolveScriptPaths(
    const NomadConfiguration& configuration,
    const NomadPath& projectDirectory
);

[[nodiscard]] std::vector<SourceRoot> getModuleScriptRoots(std::span<const ModuleManifest> modules);

[[nodiscard]] std::optional<NomadPath> getSourceRelativePath(const NomadPath& root, const NomadPath& path);

// Module roots take ownership over overlapping application roots. Repeated files are loaded once;
// conflicting module owners and dotted module identities are errors. Editor snapshots remain authoritative.
[[nodiscard]] SourceBatch loadSourceRoots(std::span<const SourceRoot> roots,
    std::span<const ScriptSource> overrides = {});

// Reads .nomad files or script roots. Each root defines its own relative script names.
// Directory traversal skips symbolic links and stops after ten directory levels.
// Loading failures are retained in the batch and prevent compilation.
// Overrides replace matching files before reading them. Unmatched overrides are added using their scriptName.
// File identities use absolute paths with canonical parent directories (case-insensitive on Windows); untitled
// overrides use exact sourceName identity. Duplicate overrides are errors, not last-write-wins.
[[nodiscard]] SourceBatch loadSourcePaths(std::span<const std::filesystem::path> paths,
    std::span<const ScriptSource> overrides = {});

} // namespace nomad
