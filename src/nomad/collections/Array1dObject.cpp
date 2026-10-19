// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/Array1dObject.hpp>

#include <nomad/script/Type.hpp>

#include <nomad/system/String.hpp>

#include <array>
#include <cstdint>

namespace nomad {

Array1dObject::Array1dObject(const Type* elementType):
    ArrayObject(elementType)
{}

Array1dObject::Array1dObject(const Type* elementType, const NomadInteger size):
    ArrayObject(elementType, std::array{size}),
    m_size(static_cast<NomadIndex>(size))
{}

NomadIndex Array1dObject::getDimensionCount() const {
    return 1;
}

NomadIndex Array1dObject::getDimensionSize(const NomadIndex dimension) const {
    if (dimension != 0) {
        throw NomadException("Invalid array dimension " + toString(dimension));
    }
    return m_size;
}

void Array1dObject::getValue(const NomadInteger index, RuntimeValue& result) const {
    const auto offset = getOffset(index);
    if (offset == NOMAD_INVALID_INDEX) {
        m_elementType->initValue(result);
    } else {
        m_elementType->copyValue(m_values[offset], result);
    }
}

void Array1dObject::setValue(const NomadInteger index, const RuntimeValue& value) {
    const auto offset = getOffset(index);
    if (offset != NOMAD_INVALID_INDEX) {
        m_elementType->assignValue(value, m_values[offset]);
    }
}

NomadIndex Array1dObject::getOffset(const NomadInteger index) const {
    if (index < 0 || static_cast<std::uintmax_t>(index) >= m_size) {
        reportIndexOutOfBounds(index, 0, m_size);
        return NOMAD_INVALID_INDEX;
    }
    return static_cast<NomadIndex>(index);
}

} // namespace nomad
