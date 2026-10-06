// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#ifndef NOMAD_BOOLEAN_HPP
#define NOMAD_BOOLEAN_HPP

#include <nomad/Nomad.hpp>

namespace nomad {

// Boolean operations
inline NomadBoolean booleanAnd(NomadBoolean a, NomadBoolean b) {
    return a && b;
}

inline NomadBoolean booleanOr(NomadBoolean a, NomadBoolean b) {
    return a || b;
}

inline NomadBoolean booleanNot(NomadBoolean a) {
    return !a;
}

inline NomadBoolean booleanEqualTo(NomadBoolean a, NomadBoolean b) {
    return a == b;
}

inline NomadBoolean booleanNotEqualTo(NomadBoolean a, NomadBoolean b) {
    return a != b;
}

} // namespace nomad

#endif // NOMAD_BOOLEAN_HPP
