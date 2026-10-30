// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/Source.hpp>

#include <filesystem>
#include <optional>
#include <vector>

namespace nomad {

class Runtime;

struct InstructionDumpResult {
    CompilationResult compilation;
    NomadString instructions;
};

struct DocumentationResult {
    CompilationResult compilation;
    NomadString documentation;
};

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
void validateEntryFunction(
    CompilationResult& result,
    const Runtime* runtime,
    const NomadString& entryFunction,
    const std::filesystem::path& projectFile
);

// Compiles without executing and formats generated instructions for every function or one named function.
[[nodiscard]] InstructionDumpResult dumpInstructions(
    const std::filesystem::path& path,
    const std::optional<NomadString>& functionName = std::nullopt,
    Runtime* runtime = nullptr
);
[[nodiscard]] InstructionDumpResult dumpInstructions(
    const std::vector<std::filesystem::path>& paths,
    const std::optional<NomadString>& functionName = std::nullopt,
    Runtime* runtime = nullptr
);
[[nodiscard]] InstructionDumpResult dumpInstructions(
    const std::vector<std::filesystem::path>& paths,
    const std::optional<NomadString>& functionName,
    Compiler& compiler
);

// Uses the configured compiler without executing scripts and generates Markdown documentation for its runtime API.
[[nodiscard]] DocumentationResult generateDocumentationForPath(
    const std::filesystem::path& path,
    Compiler& compiler
);
[[nodiscard]] DocumentationResult generateDocumentationForPaths(
    const std::vector<std::filesystem::path>& paths,
    Compiler& compiler
);

} // namespace nomad
