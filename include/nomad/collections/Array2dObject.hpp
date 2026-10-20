// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/collections/ArrayObject.hpp>

namespace nomad {

class Array2dObject final : public ArrayObject {
public:
    explicit Array2dObject(const Type* elementType);
    Array2dObject(const Type* elementType, NomadInteger size1, NomadInteger size2);

    [[nodiscard]] NomadIndex getDimensionCount() const override;
    [[nodiscard]] NomadIndex getDimensionSize(NomadIndex dimension) const override;
    // The result must be empty; the caller releases it using the element type, including on failure.
    void getValue(NomadInteger index1, NomadInteger index2, RuntimeValue& result) const;
    void setValue(NomadInteger index1, NomadInteger index2, const RuntimeValue& value);

private:
    [[nodiscard]] NomadIndex getOffset(NomadInteger index1, NomadInteger index2) const;
    NomadIndex m_size1 = 0;
    NomadIndex m_size2 = 0;
};

} // namespace nomad
