// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/CompilerContext.hpp>

#include <filesystem>
#include <optional>
#include <vector>

namespace nomad {

// The name identifies the source in diagnostics; it need not refer to a file on disk.
struct SourceText {
    NomadString sourceName;
    NomadString text;
    // Identifies the backing file, if any. The supplied text remains authoritative.
    std::optional<std::filesystem::path> filePath = std::nullopt;
};

struct ScriptSource {
    NomadString scriptName;
    SourceText source;
};

struct SourceBatch {
    std::vector<ScriptSource> sources;
    std::vector<Diagnostic> diagnostics;
};

} // namespace nomad
