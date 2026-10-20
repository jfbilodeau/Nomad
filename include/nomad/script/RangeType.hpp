// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class RangeType final : public ObjectType {
public:
    RangeType();
    void initValue(RuntimeValue& value) const override;
};

} // namespace nomad
