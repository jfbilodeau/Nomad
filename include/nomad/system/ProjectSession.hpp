// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/system/ModuleHost.hpp>

namespace nomad {

[[nodiscard]] NomadPath getSdkDirectory();
[[nodiscard]] std::vector<ModuleManifest> resolveProjectModules(
    const NomadProject& project,
    const NomadPath& sdkDirectory
);
[[nodiscard]] std::unique_ptr<ModuleHost> createProjectSession(
    const NomadProject& project,
    ModuleMode mode,
    std::vector<NomadString> arguments = {}
);

} // namespace nomad
