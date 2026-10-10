// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "nomad/Nomad.hpp"
#include "nomad/NomadVersion.hpp"

namespace nomad {

inline constexpr NomadInteger NOMAD_PROJECT_SCHEMA_VERSION = 1;
inline constexpr NomadStringView NOMAD_PROJECT_FILE_NAME = "nomad.toml";

enum class ProjectConfigurationType {
    Development = 1,
    Release
};

struct ProjectMetadata {
    NomadString name;
    NomadString identifier;
    NomadString version;
    NomadString executable;
    NomadString entry = "init";
};

struct NomadSdkConfiguration {
    NomadVersion version{0, 0, 0};
};

struct ResourceConfiguration {
    NomadPath directory = "res";
};

struct PackageConfiguration {
    NomadPath output = "dist";
    std::vector<NomadString> exclude;
};

struct ProjectConfiguration {
    NomadInteger schema = NOMAD_PROJECT_SCHEMA_VERSION;
    ProjectConfigurationType type = ProjectConfigurationType::Development;
    ProjectMetadata project;
    NomadSdkConfiguration nomad;
    ResourceConfiguration resources;
    PackageConfiguration package;
    NomadPath root;
};

class ProjectConfigurationError final : public NomadException {
public:
    using NomadException::NomadException;
};

[[nodiscard]] 
std::optional<NomadPath> findProjectRoot(
    const NomadPath& startPath
);

[[nodiscard]] ProjectConfiguration loadProjectConfiguration(
    const NomadPath& projectFile
);

[[nodiscard]] ProjectConfiguration discoverProjectConfiguration(
    const NomadPath& startPath
);

[[nodiscard]] NomadPath resolveProjectResourcePath(
    const ProjectConfiguration& configuration
);

[[nodiscard]] NomadString serializeReleaseProjectConfiguration(
    const ProjectConfiguration& configuration
);

void validateNomadVersionCompatibility(
    const NomadVersion& requiredVersion,
    const NomadVersion& availableVersion
);

} // namespace nomad
