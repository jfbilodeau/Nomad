// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/ModuleHost.hpp>

#include <nomad/system/ModuleHostImpl.hpp>

#include <utility>

namespace nomad {

std::unique_ptr<ModuleHost> ModuleHost::create(
    NomadConfiguration configuration,
    NomadPath projectDirectory,
    std::vector<ModuleManifest> modules,
    std::vector<NomadString> arguments,
    const ModuleMode mode,
    ProjectMetadata projectMetadata
) {
    auto host = std::make_unique<ModuleHostImpl>(
        std::move(configuration), std::move(projectDirectory),
        std::move(arguments), mode, std::move(projectMetadata)
    );
    host->loadModules(modules);
    return host;
}

} // namespace nomad
