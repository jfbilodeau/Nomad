// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Compilation.hpp>

#include <filesystem>

namespace nomad {

class Runtime;

// Opt-in project policy: require a parameterless script entry returning void.
void validateEntryFunction(
    CompilationResult& result,
    const Runtime* runtime,
    const NomadString& entryFunction,
    const std::filesystem::path& projectFile
);

} // namespace nomad
