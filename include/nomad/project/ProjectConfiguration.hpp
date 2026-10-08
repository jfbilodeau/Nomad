// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "nomad/Nomad.hpp"

namespace nomad {

inline constexpr NomadInteger NOMAD_PROJECT_SCHEMA_VERSION = 1;
inline constexpr NomadStringView NOMAD_PROJECT_FILE_NAME = "nomad.toml";

struct ProjectMetadata {
    NomadString name;
    NomadString identifier;
    NomadString version;
    NomadString executable;
    NomadString entry = "init";
};

struct NomadSdkConfiguration {
    NomadString version;
};

struct ResourceConfiguration {
    NomadPath directory;
};

struct PackageConfiguration {
    NomadPath output;
    std::vector<NomadString> exclude;
};

struct ProjectConfiguration {
    NomadInteger schema = NOMAD_PROJECT_SCHEMA_VERSION;
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

} // namespace nomad
