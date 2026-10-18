// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ListObject.hpp>

#include <nomad/script/Type.hpp>

#include <nomad/system/String.hpp>

namespace nomad {

ListObject::ListObject(const Type* elementType):
    m_elementType(elementType)
{
    if (elementType == nullptr || elementType->isVoid() || elementType->isInternal()) {
        throw NomadException("List element type must be a public, non-void type");
    }
}

ListObject::~ListObject() {
    if (m_elementType->needsFree()) {
        for (auto& value : m_values) {
            m_elementType->freeValue(value);
        }
    }
}

const Type* ListObject::getElementType() const {
    return m_elementType;
}

NomadIndex ListObject::getCount() const {
    return m_values.size();
}

void ListObject::push(const RuntimeValue& value) {
    RuntimeValue storedValue;
    m_elementType->initValue(storedValue);
    try {
        m_elementType->copyValue(value, storedValue);
        m_values.push_back(storedValue);
    } catch (...) {
        m_elementType->freeValue(storedValue);
        throw;
    }
}

NomadIndex ListObject::validateIndex(const NomadInteger index) const {
    if (index < 0 || static_cast<NomadIndex>(index) >= m_values.size()) {
        throw NomadException("List index " + toString(index) + " is out of bounds for size " + toString(m_values.size()));
    }
    return static_cast<NomadIndex>(index);
}

const RuntimeValue& ListObject::getValue(const NomadInteger index) const {
    return m_values[validateIndex(index)];
}

void ListObject::setValue(const NomadInteger index, const RuntimeValue& value) {
    auto& destination = m_values[validateIndex(index)];
    RuntimeValue replacement;
    m_elementType->initValue(replacement);
    try {
        m_elementType->copyValue(value, replacement);
    } catch (...) {
        m_elementType->freeValue(replacement);
        throw;
    }
    m_elementType->freeValue(destination);
    destination = replacement;
}

} // namespace nomad
