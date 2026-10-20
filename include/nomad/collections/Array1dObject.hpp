// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/collections/ArrayObject.hpp>

namespace nomad {

class Array1dObject final : public ArrayObject {
public:
    explicit Array1dObject(const Type* elementType);
    Array1dObject(const Type* elementType, NomadInteger size);

    [[nodiscard]] NomadIndex getDimensionCount() const override;
    [[nodiscard]] NomadIndex getDimensionSize(NomadIndex dimension) const override;
    // The result must be empty; the caller releases it using the element type, including on failure.
    void getValue(NomadInteger index, RuntimeValue& result) const;
    void setValue(NomadInteger index, const RuntimeValue& value);

private:
    [[nodiscard]] NomadIndex getOffset(NomadInteger index) const;
    NomadIndex m_size = 0;
};

} // namespace nomad
