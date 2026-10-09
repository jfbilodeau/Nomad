// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/project/ProjectConfiguration.hpp>

#include <vector>

namespace nomad {

struct ProjectPackageResult {
    NomadPath output;
    std::vector<NomadPath> files;
};

class ProjectPackagingError final : public NomadException {
public:
    using NomadException::NomadException;
};

[[nodiscard]] ProjectPackageResult packageProject(
    const ProjectConfiguration& configuration,
    const NomadPath& runtimeDirectory,
    bool force,
    bool dryRun = false
);

} // namespace nomad
