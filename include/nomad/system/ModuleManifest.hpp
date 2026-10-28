// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <optional>
#include <vector>

namespace nomad {

struct ModuleManifest {
    NomadString name;
    NomadPath directory;
    std::vector<NomadString> dependencies;
    std::optional<NomadPath> scriptsDirectory = std::nullopt;
    bool scriptsDirectoryRequired = false;
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
