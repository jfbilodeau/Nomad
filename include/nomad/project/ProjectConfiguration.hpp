// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>
#include <nomad/NomadVersion.hpp>

#include <nomad/project/ProjectMetadata.hpp>

#include <nomad/system/NomadConfiguration.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace nomad {

inline constexpr NomadInteger NOMAD_PROJECT_SCHEMA_VERSION = 1;
inline constexpr NomadStringView NOMAD_PROJECT_FILE_NAME = "nomad.toml";

enum class ProjectConfigurationType {
    Development = 1,
    Release
};

struct SdkConfiguration {
    NomadVersion version{0, 0, 0};
    std::vector<NomadString> modules;
};

struct PackageConfiguration {
    NomadPath output = "dist";
    std::vector<NomadString> exclude;
};

struct ProjectConfiguration {
    NomadInteger schema = NOMAD_PROJECT_SCHEMA_VERSION;
    ProjectConfigurationType type = ProjectConfigurationType::Development;
    ProjectMetadata project;
    SdkConfiguration sdk;
    PackageConfiguration package;
};

struct NomadProject {
    ProjectConfiguration configuration;
    NomadPath root;
    NomadConfiguration document;
};

[[nodiscard]] 
std::optional<NomadPath> findProjectRoot(
    const NomadPath& startPath
);

[[nodiscard]] NomadProject loadNomadProject(
    const NomadPath& projectFile
);

[[nodiscard]] NomadProject discoverNomadProject(
    const NomadPath& startPath
);

[[nodiscard]] NomadString serializeReleaseProject(
    const NomadProject& project
);

void validateNomadVersionCompatibility(
    const NomadVersion& requiredVersion,
    const NomadVersion& availableVersion
);

} // namespace nomad
