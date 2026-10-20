// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class ListType final : public ObjectType {
public:
    explicit ListType(const Type* elementType);

    void initValue(RuntimeValue& value) const override;
};

} // namespace nomad
