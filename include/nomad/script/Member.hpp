// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Callable.hpp>

#include <vector>

namespace nomad {

enum class MemberType {
    Unknown = 1,
    Property,
    Method,
};

struct MemberSignature {
    MemberType type = MemberType::Unknown;
    std::vector<Parameter> parameters;
};

} // namespace nomad
