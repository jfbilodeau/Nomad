// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/RangeType.hpp>

#include <nomad/collections/RangeObject.hpp>

namespace nomad {

RangeType::RangeType():
    ObjectType("Range", {}, "An immutable integer sequence with an exclusive stop.")
{}

void RangeType::initValue(RuntimeValue& value) const {
    value.setObjectValue(new RangeObject(0, 0, 1));
}

} // namespace nomad
