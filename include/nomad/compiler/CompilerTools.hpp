// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Source.hpp>

#include <filesystem>
#include <vector>

namespace nomad {

class Runtime;

// Compiles a Nomad source file or every Nomad source below a directory without executing any function.
// Pass the runtime of a headless `Game` to resolve engine API symbols; null compiles against language built-ins only.
[[nodiscard]] CompilationResult checkPath(const std::filesystem::path& path, Runtime* runtime = nullptr);
[[nodiscard]] CompilationResult checkPaths(
    const std::vector<std::filesystem::path>& paths,
    Runtime* runtime = nullptr
);
// Uses supplied snapshots without executing scripts. Additional roots configured on the runtime's compiler
// are also discovered; supplied snapshots override their backing files in those roots.
[[nodiscard]] CompilationResult checkSources(const SourceBatch& batch, Runtime* runtime = nullptr);
[[nodiscard]] CompilationResult analyzeSources(const SourceBatch& batch, Runtime* runtime = nullptr);
} // namespace nomad
