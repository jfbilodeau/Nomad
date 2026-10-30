// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Diagnostic.hpp>

#include <vector>

namespace nomad {

class Compiler;
struct SourceBatch;

enum class CompilationPhase {
    Configure = 1,
    PreParse,
    Parse,
    PrepareFormatStrings,
    Resolve,
    Emit,
    Link,
    Verify,
    Completed,
};

enum class CompilationTarget {
    Analysis = 1,
    Executable,
};

struct CompilationResult {
    std::vector<Diagnostic> diagnostics;
    NomadIndex errorCount = 0;
    NomadIndex warningCount = 0;

    [[nodiscard]] bool succeeded() const;
};

// Uses the supplied compiler and source snapshots without executing scripts.
[[nodiscard]] CompilationResult compileSources(
    const SourceBatch& batch,
    Compiler& compiler,
    CompilationTarget target = CompilationTarget::Executable
);

} // namespace nomad
