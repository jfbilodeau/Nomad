// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ListObject.hpp>

#include <nomad/script/Type.hpp>

#include <nomad/system/String.hpp>

#include <utility>

namespace nomad {

ListObject::ListObject(const Type* elementType):
    m_elementType(elementType)
{
    if (elementType == nullptr || elementType->isVoid() || elementType->isInternal()) {
        throw NomadException("List element type must be a public, non-void type");
    }
}

ListObject::~ListObject() {
    clear();
}

void ListObject::clear() {
    if (!m_values.empty()) {
        ++m_structuralVersion;
    }
    if (m_elementType->needsFree()) {
        for (auto& value : m_values) {
            m_elementType->freeValue(&value);
        }
    }
    m_values.clear();
}

const Type* ListObject::getElementType() const {
    return m_elementType;
}

NomadIndex ListObject::getCount() const {
    return m_values.size();
}

NomadInteger ListObject::append(const RuntimeValue& value) {
    if (!std::in_range<NomadInteger>(m_values.size())) {
        throw NomadException("List size exceeds the maximum index");
    }
    const auto index = static_cast<NomadInteger>(m_values.size());
    insert(index, value);
    return index;
}

void ListObject::prepend(const RuntimeValue& value) {
    insert(0, value);
}

void ListObject::insert(const NomadInteger index, const RuntimeValue& value) {
    if (index < 0 || static_cast<NomadIndex>(index) > m_values.size()) {
        throw NomadException("List insertion index " + toString(index) +
            " is out of bounds for size " + toString(m_values.size()));
    }
    RuntimeValue storedValue;
    try {
        m_elementType->copyValue(&value, &storedValue);
        m_values.insert(m_values.begin() + static_cast<std::vector<RuntimeValue>::difference_type>(index), storedValue);
        ++m_structuralVersion;
    } catch (...) {
        m_elementType->freeValue(&storedValue);
        throw;
    }
}

bool ListObject::remove(const NomadInteger index) {
    if (index < 0 || static_cast<NomadIndex>(index) >= m_values.size()) {
        return false;
    }
    const auto position = m_values.begin() + static_cast<std::vector<RuntimeValue>::difference_type>(index);
    if (m_elementType->needsFree()) {
        m_elementType->freeValue(&*position);
    }
    m_values.erase(position);
    ++m_structuralVersion;
    return true;
}

bool ListObject::removeFirst() {
    return remove(0);
}

bool ListObject::removeLast() {
    if (m_values.empty()) {
        return false;
    }
    return remove(static_cast<NomadInteger>(m_values.size() - 1));
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
    m_elementType->assignValue(value, m_values[validateIndex(index)]);
}

} // namespace nomad
