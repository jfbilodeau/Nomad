// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/ProjectSession.hpp>

#include <nomad/Version.hpp>

#include <nomad/system/Path.hpp>

#include <boost/dll/runtime_symbol_info.hpp>

namespace nomad {

NomadPath getSdkDirectory() {
    return NomadPath(boost::dll::program_location().native()).parent_path();
}

std::vector<ModuleManifest> resolveProjectModules(
    const NomadProject& project,
    const NomadPath& sdkDirectory
) {
    if (project.configuration.type == ProjectConfigurationType::Development) {
        return resolveModules(sdkDirectory / "modules", project.configuration.sdk.modules);
    }
    std::vector<ModuleManifest> modules;
    for (const auto& name : project.configuration.sdk.modules) {
        if (!isValidModuleName(name)) {
            throw ModuleError("Invalid packaged module name '" + name + "'");
        }
        modules.push_back({name, project.root, {}});
    }
    return modules;
}

std::unique_ptr<ModuleHost> createProjectSession(
    const NomadProject& project,
    const ModuleMode mode,
    std::vector<NomadString> arguments
) {
    validateNomadVersionCompatibility(project.configuration.sdk.version, getNomadVersion());
    return std::make_unique<ModuleHost>(
        project.document, project.root, resolveProjectResourcePath(project),
        resolveProjectModules(project, getSdkDirectory()), std::move(arguments), mode
    );
}

} // namespace nomad
