// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

namespace nomad {

struct ProjectMetadata {
    NomadString name;
    NomadString identifier;
    NomadString version;
    NomadString executable;
};

} // namespace nomad
