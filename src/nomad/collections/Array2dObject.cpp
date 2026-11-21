// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array2dObject.hpp>

#include <nomad/script/Type.hpp>

#include <nomad/system/String.hpp>

#include <array>
#include <cstdint>

namespace nomad {

Array2dObject::Array2dObject(const Type* elementType):
    ArrayObject(elementType)
{}

Array2dObject::Array2dObject(
    const Type* elementType,
    const NomadInteger size1,
    const NomadInteger size2
):
    ArrayObject(elementType, std::array{size1, size2}),
    m_size1(static_cast<NomadIndex>(size1)),
    m_size2(static_cast<NomadIndex>(size2))
{}

NomadIndex Array2dObject::getDimensionCount() const {
    return 2;
}

NomadIndex Array2dObject::getDimensionSize(const NomadIndex dimension) const {
    switch (dimension) {
    case 0: return m_size1;
    case 1: return m_size2;
    default: throw NomadException("Invalid array dimension " + toString(dimension));
    }
}

void Array2dObject::getValue(
    const NomadInteger index1,
    const NomadInteger index2,
    RuntimeValue& result
) const {
    const auto offset = getOffset(index1, index2);
    if (offset == NOMAD_INVALID_INDEX) {
        m_elementType->initValue(&result);
    } else {
        m_elementType->copyValue(&m_values[offset], &result);
    }
}

void Array2dObject::setValue(
    const NomadInteger index1,
    const NomadInteger index2,
    const RuntimeValue& value
) {
    const auto offset = getOffset(index1, index2);
    if (offset != NOMAD_INVALID_INDEX) {
        m_elementType->assignValue(&value, &m_values[offset]);
    }
}

NomadIndex Array2dObject::getOffset(const NomadInteger index1, const NomadInteger index2) const {
    if (index1 < 0 || static_cast<std::uintmax_t>(index1) >= m_size1) {
        reportIndexOutOfBounds(index1, 0, m_size1);
        return NOMAD_INVALID_INDEX;
    }
    if (index2 < 0 || static_cast<std::uintmax_t>(index2) >= m_size2) {
        reportIndexOutOfBounds(index2, 1, m_size2);
        return NOMAD_INVALID_INDEX;
    }
    return static_cast<NomadIndex>(index1) * m_size2 + static_cast<NomadIndex>(index2);
}

} // namespace nomad
