// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/CompilerContext.hpp>

#include <filesystem>
#include <optional>
#include <vector>

namespace nomad {

struct CompilationResult {
    std::vector<Diagnostic> diagnostics;
    NomadIndex errorCount = 0;
    NomadIndex warningCount = 0;

    [[nodiscard]] bool succeeded() const;
};

struct InstructionDumpResult {
    CompilationResult compilation;
    NomadString instructions;
};

struct DocumentationResult {
    CompilationResult compilation;
    NomadString documentation;
};

// Compiles a Nomad source file or every Nomad source below a directory without executing any function.
[[nodiscard]] CompilationResult checkPath(const std::filesystem::path& path);

// Compiles without executing and formats generated instructions for every function or one named function.
[[nodiscard]] InstructionDumpResult dumpInstructions(
    const std::filesystem::path& path,
    const std::optional<NomadString>& functionName = std::nullopt
);

// Compiles without executing and generates Markdown documentation for the registered language API.
[[nodiscard]] DocumentationResult generateDocumentationForPath(const std::filesystem::path& path);

} // namespace nomad
