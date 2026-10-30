// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

struct ModuleManifest {
    NomadString name;
    NomadPath directory;
    std::vector<NomadString> dependencies;
};

class ModuleException final : public NomadException {
public:
    using NomadException::NomadException;
};

[[nodiscard]] bool isValidModuleName(NomadStringView name);
[[nodiscard]] NomadPath getModuleLibraryName(NomadStringView name);
[[nodiscard]] ModuleManifest loadModuleManifest(const NomadPath& directory);
[[nodiscard]] std::vector<ModuleManifest> resolveModules(
    const NomadPath& modulesDirectory,
    const std::vector<NomadString>& requestedModules
);

} // namespace nomad
