// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ArrayObject.hpp>

#include <nomad/collections/Array1dObject.hpp>
#include <nomad/collections/Array2dObject.hpp>
#include <nomad/collections/Array3dObject.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Type.hpp>

#include <nomad/system/String.hpp>

#include <cstdint>

namespace nomad {

ArrayObject::ArrayObject(const Type* elementType):
    m_elementType(elementType)
{
    if (elementType == nullptr || elementType->isVoid() || elementType->isInternal()) {
        throw NomadException("Array element type must be a public, non-void type");
    }
}

ArrayObject::ArrayObject(const Type* elementType, const std::span<const NomadInteger> dimensions):
    ArrayObject(elementType)
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
        count *= static_cast<NomadIndex>(size);
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

std::unique_ptr<ArrayObject> ArrayObject::createCopy() const {
    const auto size = [this](const NomadIndex dimension) {
        return static_cast<NomadInteger>(getDimensionSize(dimension));
    };
    std::unique_ptr<ArrayObject> snapshot;
    switch (getDimensionCount()) {
    case 1:
        snapshot = m_values.empty() ? std::make_unique<Array1dObject>(m_elementType) :
            std::make_unique<Array1dObject>(m_elementType, size(0));
        break;
    case 2:
        snapshot = m_values.empty() ? std::make_unique<Array2dObject>(m_elementType) :
            std::make_unique<Array2dObject>(m_elementType, size(0), size(1));
        break;
    case 3:
        snapshot = m_values.empty() ? std::make_unique<Array3dObject>(m_elementType) :
            std::make_unique<Array3dObject>(m_elementType, size(0), size(1), size(2));
        break;
    default:
        throw NomadException("Invalid array rank");
    }
    for (auto index = NomadIndex{0}; index < m_values.size(); ++index) {
        m_elementType->assignValue(m_values[index], snapshot->m_values[index]);
    }
    return snapshot;
}

void ArrayObject::reportIndexOutOfBounds(
    const NomadInteger index,
    const NomadIndex dimension,
    const NomadIndex size
) {
    log::error("Array index " + toString(index) + " is out of bounds for dimension " +
        toString(dimension + 1) + " of size " + toString(size));
}

} // namespace nomad
