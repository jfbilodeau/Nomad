// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <span>
#include <vector>

namespace nomad {

class Type;

class ArrayObject : public NomadObject {
public:
    ~ArrayObject() override;

    [[nodiscard]] const Type* getElementType() const;
    [[nodiscard]] NomadIndex getCount() const;
    [[nodiscard]] NomadIndex getDimensionCount() const;
    [[nodiscard]] NomadIndex getDimensionSize(NomadIndex dimension) const;

protected:
    ArrayObject(const Type* elementType, NomadIndex dimensionCount);
    ArrayObject(const Type* elementType, std::span<const NomadInteger> dimensions);

    [[noreturn]] static void reportIndexOutOfBounds(NomadInteger index, NomadIndex dimension, NomadIndex size);

    const Type* m_elementType;
    std::vector<NomadIndex> m_dimensions;
    std::vector<RuntimeValue> m_values;

private:
    void freeElements() noexcept;
};

} // namespace nomad
