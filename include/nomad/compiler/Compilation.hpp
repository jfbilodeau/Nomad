// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/CompilerContext.hpp>

#include <filesystem>
#include <vector>

namespace nomad {

struct CompilationResult {
    std::vector<Diagnostic> diagnostics;
    NomadIndex errorCount = 0;
    NomadIndex warningCount = 0;

    [[nodiscard]] bool succeeded() const;
};

// Compiles a Nomad source file or every Nomad source below a directory without executing any function.
[[nodiscard]] CompilationResult checkPath(const std::filesystem::path& path);

} // namespace nomad
