// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/collections/ArrayType.hpp>

namespace nomad {

class Array1dObject;

class Array1Type final : public ArrayType {
public:
    explicit Array1Type(const Type* elementType);

    [[nodiscard]] Array1dObject* createObject(NomadInteger size) const;

protected:
    [[nodiscard]] ArrayObject* createEmptyArray() const override;
};

} // namespace nomad
