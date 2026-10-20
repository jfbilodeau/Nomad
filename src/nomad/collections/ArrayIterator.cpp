// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/ArrayIterator.hpp>

#include <nomad/collections/Array1dObject.hpp>

namespace nomad {

ArrayIterator::ArrayIterator(Array1dObject* array):
    IteratorObject(array->getElementType()), m_array(array)
{
    m_array->addRef();
}

ArrayIterator::~ArrayIterator() {
    m_array->releaseRef();
}

bool ArrayIterator::onMoveNext(RuntimeValue& value) {
    if (m_next == m_array->getCount()) {
        return false;
    }
    m_array->getValue(static_cast<NomadInteger>(m_next), value);
    ++m_next;
    return true;
}

} // namespace nomad
