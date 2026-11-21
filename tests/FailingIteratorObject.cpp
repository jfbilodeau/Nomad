// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <FailingIteratorObject.hpp>

namespace nomad::test {

FailingIteratorObject::FailingIteratorObject(const Type* valueType, const RuntimeValue& source):
    IteratorObject(valueType),
    m_valueType(valueType),
    m_source(source)
{}

NomadIndex FailingIteratorObject::getCallCount() const {

    return m_callCount;
}

bool FailingIteratorObject::onMoveNext(RuntimeValue& value) {
    if (++m_callCount > 3) {
        return false;
    }

    m_valueType->copyValue(&m_source, &value);
    if (m_callCount == 2) {
        throw NomadException("Expected partial iterator advancement failure");
    }

    return true;
}

} // namespace nomad::test
