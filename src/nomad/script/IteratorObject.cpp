// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/IteratorObject.hpp>

namespace nomad {

IteratorObject::IteratorObject(const Type* valueType): m_valueType(valueType) {
    if (valueType == nullptr || valueType->isVoid() || valueType->isInternal()) {
        throw NomadException("Iterator requires a public non-void value type");
    }
}

IteratorObject::~IteratorObject() {
    m_valueType->freeValue(m_current);
}

bool IteratorObject::moveNext() {
    m_valid = false;
    m_valueType->freeValue(m_current);
    m_current = RuntimeValue{};
    if (m_completed) {
        return false;
    }
    try {
        m_valid = onMoveNext(m_current);
    } catch (...) {
        m_valueType->freeValue(m_current);
        m_current = RuntimeValue{};
        throw;
    }
    m_completed = !m_valid;
    return m_valid;
}

const RuntimeValue& IteratorObject::getCurrent() const {
    if (!m_valid) {
        throw NomadException("Iterator has no current value");
    }
    return m_current;
}

} // namespace nomad
