// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array2dObject.hpp>

#include <nomad/script/Type.hpp>

#include <array>
#include <cstdint>

namespace nomad {

Array2dObject::Array2dObject(const Type* elementType):
    ArrayObject(elementType, 2)
{}

Array2dObject::Array2dObject(
    const Type* elementType,
    const NomadInteger size1,
    const NomadInteger size2
):
    ArrayObject(elementType, std::array{size1, size2})
{}

const RuntimeValue& Array2dObject::getValue(const NomadInteger index1, const NomadInteger index2) const {
    return m_values[getOffset(index1, index2)];
}

void Array2dObject::setValue(
    const NomadInteger index1,
    const NomadInteger index2,
    const RuntimeValue& value
) {
    m_elementType->assignValue(value, m_values[getOffset(index1, index2)]);
}

NomadIndex Array2dObject::getOffset(const NomadInteger index1, const NomadInteger index2) const {
    if (index1 < 0 || static_cast<std::uintmax_t>(index1) >= m_dimensions[0]) {
        reportIndexOutOfBounds(index1, 0, m_dimensions[0]);
    }
    if (index2 < 0 || static_cast<std::uintmax_t>(index2) >= m_dimensions[1]) {
        reportIndexOutOfBounds(index2, 1, m_dimensions[1]);
    }
    return static_cast<NomadIndex>(index1) * m_dimensions[1] + static_cast<NomadIndex>(index2);
}

} // namespace nomad
