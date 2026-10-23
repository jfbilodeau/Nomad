// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Callable.hpp>
#include <nomad/script/Property.hpp>

#include <variant>
#include <vector>

namespace nomad {

using MemberReference = std::variant<PropertyId, CallableId>;

inline std::ostream& operator<<(std::ostream& output, const MemberReference& member) {
    return std::visit([&output](const auto id) -> std::ostream& { return output << id; }, member);
}

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
