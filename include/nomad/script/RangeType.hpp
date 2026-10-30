// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class RangeType final : public ClassType {
public:
    RangeType(Runtime* runtime, TypeId id, NomadString name, ClassMode mode);
    void initValue(RuntimeValue& value) const override;
};

} // namespace nomad
