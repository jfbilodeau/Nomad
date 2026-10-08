// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

struct ProjectInitializationResult {
    NomadPath root;
    std::vector<NomadPath> createdFiles;
};

class ProjectInitializationError final : public NomadException {
public:
    using NomadException::NomadException;
};

[[nodiscard]] NomadString makeProjectExecutableName(NomadStringView projectName);

[[nodiscard]] ProjectInitializationResult initializeProject(
    const NomadPath& destination,
    const NomadPath& templateDirectory
);

} // namespace nomad
