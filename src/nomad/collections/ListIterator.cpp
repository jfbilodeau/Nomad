// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ListIterator.hpp>

#include <nomad/collections/ListObject.hpp>

namespace nomad {

ListIterator::ListIterator(ListObject* list):
    IteratorObject(list->getElementType()), m_list(list), m_version(list->getStructuralVersion())
{
    m_list->addRef();
}

ListIterator::~ListIterator() {
    m_list->releaseRef();
}

bool ListIterator::onMoveNext(RuntimeValue& value) {
    if (m_list->getStructuralVersion() != m_version) {
        throw NomadException("List was structurally modified during iteration");
    }
    if (m_next == m_list->getCount()) {
        return false;
    }
    m_list->getElementType()->copyValue(m_list->getValue(static_cast<NomadInteger>(m_next)), value);
    ++m_next;
    return true;
}

} // namespace nomad
