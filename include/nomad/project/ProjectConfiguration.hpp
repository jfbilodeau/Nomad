// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>
#include <nomad/NomadVersion.hpp>

#include <nomad/game/GameOptions.hpp>

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

struct ProjectMetadata {
    NomadString name;
    NomadString identifier;
    NomadString version;
    NomadString executable;
};

struct SdkConfiguration {
    NomadVersion version{0, 0, 0};
    std::vector<NomadString> modules;
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
    SdkConfiguration sdk;
    ResourceConfiguration resources;
    PackageConfiguration package;
};

struct NomadProject {
    ProjectConfiguration configuration;
    GameOptions gameOptions;
    NomadPath root;
    NomadConfiguration document;
};

class ProjectConfigurationError final : public NomadException {
public:
    using NomadException::NomadException;
};

[[nodiscard]] 
std::optional<NomadPath> findProjectRoot(
    const NomadPath& startPath
);

[[nodiscard]] NomadProject loadNomadProject(
    const NomadPath& projectFile
);

[[nodiscard]] GameOptions readGameOptions(
    const NomadConfiguration& configuration
);

[[nodiscard]] NomadProject discoverNomadProject(
    const NomadPath& startPath
);

[[nodiscard]] NomadPath resolveProjectResourcePath(
    const NomadProject& project
);

[[nodiscard]] NomadString serializeReleaseProject(
    const NomadProject& project
);

void validateNomadVersionCompatibility(
    const NomadVersion& requiredVersion,
    const NomadVersion& availableVersion
);

} // namespace nomad
