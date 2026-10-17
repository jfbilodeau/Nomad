// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ListObject.hpp>

#include <nomad/script/Type.hpp>

namespace nomad {

ListObject::ListObject(const Type* elementType):
    m_elementType(elementType)
{
    if (elementType == nullptr || elementType->isVoid() || elementType->isInternal()) {
        throw NomadException("List element type must be a public, non-void type");
    }
}

ListObject::~ListObject() = default;

const Type* ListObject::getElementType() const {
    return m_elementType;
}

NomadIndex ListObject::getCount() const {
    return m_values.size();
}

void ListObject::push(const RuntimeValue& value) {
    m_values.emplace_back(m_elementType, value);
}

} // namespace nomad
