// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/collections/RangeIterator.hpp>

#include <nomad/collections/RangeObject.hpp>

#include <limits>

namespace nomad {

RangeIterator::RangeIterator(
    const Type* valueType,
    const RangeObject& range
):
    IteratorObject(valueType), m_next(range.getStart()), m_stop(range.getStop()), m_step(range.getStep())
{}

bool RangeIterator::onMoveNext(RuntimeValue& value) {
    if (m_exhausted || (m_step > 0 ? m_next >= m_stop : m_next <= m_stop)) {
        return false;
    }
    value.setIntegerValue(m_next);
    if ((m_step > 0 && m_next > std::numeric_limits<NomadInteger>::max() - m_step) ||
        (m_step < 0 && m_next < std::numeric_limits<NomadInteger>::min() - m_step)) {
        m_exhausted = true;
    } else {
        m_next += m_step;
    }
    return true;
}

} // namespace nomad
