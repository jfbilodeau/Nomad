// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <type_traits>

namespace nomad {

[[nodiscard]] inline bool isNumericRangeValue(
    NomadInteger value,
    NomadInteger last,
    NomadInteger step
) {
    if (step == 0) {
        throw NomadException("Numeric for step must not be zero");
    }
    return step > 0 ? value <= last : value >= last;
}

[[nodiscard]] inline bool advanceNumericRange(
    NomadInteger& value,
    NomadInteger last,
    NomadInteger step
) {
    if (!isNumericRangeValue(value, last, step)) {
        return false;
    }
    using UnsignedInteger = std::make_unsigned_t<NomadInteger>;
    const auto distance = step > 0
        ? static_cast<UnsignedInteger>(last) - static_cast<UnsignedInteger>(value)
        : static_cast<UnsignedInteger>(value) - static_cast<UnsignedInteger>(last);
    const auto magnitude = step > 0
        ? static_cast<UnsignedInteger>(step)
        : UnsignedInteger{0} - static_cast<UnsignedInteger>(step);
    if (distance < magnitude) {
        return false;
    }
    value += step;
    return true;
}

} // namespace nomad
