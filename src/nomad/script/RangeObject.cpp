// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/RangeObject.hpp>

namespace nomad {

RangeObject::RangeObject(
    NomadInteger start,
    NomadInteger stop,
    NomadInteger step
):
    m_start(start), m_stop(stop), m_step(step)
{
    if (step == 0) {
        throw NomadException("Range step must not be zero");
    }
}

NomadInteger RangeObject::getStart() const {
    return m_start;
}

NomadInteger RangeObject::getStop() const {
    return m_stop;
}

NomadInteger RangeObject::getStep() const {
    return m_step;
}

} // namespace nomad
