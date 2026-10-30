// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

namespace nomad {

struct TypeReference {
    NomadString name;

    [[nodiscard]] bool operator==(const TypeReference&) const = default;
};

} // namespace nomad
