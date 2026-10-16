// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Source.hpp>

#include <filesystem>
#include <span>

namespace nomad {

// Reads .nomad files or script roots. Each root defines its own relative function names.
// Directory traversal skips symbolic links and stops after ten directory levels.
// Loading failures are retained in the batch and prevent compilation.
[[nodiscard]] SourceBatch loadSourcePaths(std::span<const std::filesystem::path> paths);

} // namespace nomad
