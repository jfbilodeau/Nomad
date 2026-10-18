// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ArrayObject.hpp>

#include <nomad/script/Type.hpp>

#include <nomad/system/String.hpp>

#include <cstdint>

namespace nomad {

ArrayObject::ArrayObject(const Type* elementType, const NomadIndex dimensionCount):
    m_elementType(elementType),
    m_dimensions(dimensionCount, 0)
{
    if (elementType == nullptr || elementType->isVoid() || elementType->isInternal()) {
        throw NomadException("Array element type must be a public, non-void type");
    }
}

ArrayObject::ArrayObject(const Type* elementType, const std::span<const NomadInteger> dimensions):
    ArrayObject(elementType, dimensions.size())
{
    auto count = NomadIndex{1};
    for (auto dimension = NomadIndex{0}; dimension < dimensions.size(); ++dimension) {
        const auto size = dimensions[dimension];
        if (size <= 0) {
            throw NomadException("Array dimension " + toString(dimension + 1) +
                " must be greater than zero, got " + toString(size));
        }
        if (static_cast<std::uintmax_t>(size) > m_values.max_size() / count) {
            throw NomadException("Array dimensions exceed the maximum element count");
        }
        m_dimensions[dimension] = static_cast<NomadIndex>(size);
        count *= m_dimensions[dimension];
    }

    m_values.reserve(count);
    // The delegated constructor ensures destruction also frees partial initialization.
    for (auto index = NomadIndex{0}; index < count; ++index) {
        m_values.emplace_back();
        m_elementType->initValue(m_values.back());
    }
}

ArrayObject::~ArrayObject() {
    freeElements();
}

void ArrayObject::freeElements() noexcept {
    if (!m_values.empty() && m_elementType->needsFree()) {
        for (auto& value : m_values) {
            m_elementType->freeValue(value);
        }
    }
}

const Type* ArrayObject::getElementType() const {
    return m_elementType;
}

NomadIndex ArrayObject::getCount() const {
    return m_values.size();
}

NomadIndex ArrayObject::getDimensionCount() const {
    return m_dimensions.size();
}

NomadIndex ArrayObject::getDimensionSize(const NomadIndex dimension) const {
    if (dimension >= m_dimensions.size()) {
        throw NomadException("Invalid array dimension " + toString(dimension));
    }
    return m_dimensions[dimension];
}

NomadIndex ArrayObject::getOffset(const std::span<const NomadInteger> indices) const {
    if (indices.size() != m_dimensions.size()) {
        throw NomadException("Array index count does not match its dimensions");
    }
    auto offset = NomadIndex{0};
    for (auto dimension = NomadIndex{0}; dimension < indices.size(); ++dimension) {
        const auto index = indices[dimension];
        const auto size = m_dimensions[dimension];
        if (index < 0 || static_cast<std::uintmax_t>(index) >= size) {
            throw NomadException("Array index " + toString(index) + " is out of bounds for dimension " +
                toString(dimension + 1) + " of size " + toString(size));
        }
        offset = offset * size + static_cast<NomadIndex>(index);
    }
    return offset;
}

const RuntimeValue& ArrayObject::getElement(const std::span<const NomadInteger> indices) const {
    return m_values[getOffset(indices)];
}

void ArrayObject::setElement(const std::span<const NomadInteger> indices, const RuntimeValue& value) {
    m_elementType->assignValue(value, m_values[getOffset(indices)]);
}

} // namespace nomad
