// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/IteratorObject.hpp>

#include <boost/scope_exit.hpp>

namespace nomad {

IteratorObject::IteratorObject(const Type* valueType): m_valueType(valueType) {
    if (valueType == nullptr || valueType->isVoid() || valueType->isInternal()) {
        throw NomadException("Iterator requires a public non-void value type");
    }
}

IteratorObject::~IteratorObject() {
    m_valueType->freeValue(&m_current);
}

bool IteratorObject::moveNext() {
    m_valid = false;
    m_valueType->freeValue(&m_current);
    m_current = RuntimeValue{};
    if (m_completed) {
        return false;
    }
    auto committed = false;
    BOOST_SCOPE_EXIT_ALL(&, this) {
        if (!committed) {
            m_valueType->freeValue(&m_current);
            m_current = RuntimeValue{};
        }
    };

    m_valid = onMoveNext(m_current);
    m_completed = !m_valid;
    committed = true;

    return m_valid;
}

const RuntimeValue& IteratorObject::getCurrent() const {
    if (!m_valid) {
        throw NomadException("Iterator has no current value");
    }
    return m_current;
}

} // namespace nomad
