// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <cmath>

namespace nomad {

// Float operations
inline NomadFloat floatAdd(NomadFloat a, NomadFloat b) {
    return a + b;
}

inline NomadFloat floatSubtract(NomadFloat a, NomadFloat b) {
    return a - b;
}

inline NomadFloat floatMultiply(NomadFloat a, NomadFloat b) {
    return a * b;
}

inline NomadFloat floatDivide(NomadFloat a, NomadFloat b) {
    return a / b;
}

inline NomadFloat floatModulo(NomadFloat a, NomadFloat b) {
    return std::fmod(a, b);
}

inline NomadFloat floatPower(NomadFloat a, NomadFloat b) {
    return std::pow(a, b);
}

inline NomadFloat floatAbsolute(NomadFloat a) {
    return std::abs(a);
}

inline NomadFloat floatNegate(NomadFloat a) {
    return -a;
}

inline NomadFloat floatSquareRoot(NomadFloat a) {
    return std::sqrt(a);
}

inline NomadFloat floatSin(NomadFloat a) {
    return std::sin(a);
}

inline NomadFloat floatCosine(NomadFloat a) {
    return std::cos(a);
}

inline NomadFloat floatTangent(NomadFloat a) {
    return std::tan(a);
}

inline NomadFloat floatFloor(NomadFloat a) {
    return std::floor(a);
}

inline NomadFloat floatCeiling(NomadFloat a) {
    return std::ceil(a);
}

inline NomadFloat floatRound(NomadFloat a) {
    return std::round(a);
}

inline NomadBoolean floatEqualTo(NomadFloat a, NomadFloat b) {
    return a == b;
}

inline NomadBoolean floatNotEqualTo(NomadFloat a, NomadFloat b) {
    return a != b;
}

inline NomadBoolean floatLessThan(NomadFloat a, NomadFloat b) {
    return a < b;
}

inline NomadBoolean floatLessThanOrEqual(NomadFloat a, NomadFloat b) {
    return a <= b;
}

inline NomadBoolean floatGreaterThan(NomadFloat a, NomadFloat b) {
    return a > b;
}

inline NomadBoolean floatGreaterThanOrEqual(NomadFloat a, NomadFloat b) {
    return a >= b;
}

inline NomadInteger floatToInteger(NomadFloat a) {
    return static_cast<NomadInteger>(a);
}

inline NomadString floatToString(NomadFloat a) {
    return NomadString(std::to_string(a));
}

} // namespace nomad
