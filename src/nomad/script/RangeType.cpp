// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/RangeType.hpp>

#include <nomad/script/RangeObject.hpp>

namespace nomad {

RangeType::RangeType(Runtime* runtime, TypeId id, NomadString name, const ClassMode mode):
    ClassType(runtime, id, std::move(name), mode, {}, "An immutable integer sequence with an exclusive stop.")
{}

void RangeType::initValue(RuntimeValue& value) const {
    value.setObjectValue(new RangeObject(0, 0, 1));
}

} // namespace nomad
