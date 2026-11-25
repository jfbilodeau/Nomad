// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <DynamicVariableCopyProbe.hpp>

#include <nomad/script/RuntimeValue.hpp>

namespace nomad::test {

DynamicVariableCopyProbe::DynamicVariableCopyProbe(DynamicVariableCopyCounts& counts):
    m_counts(&counts)
{}

DynamicVariableCopyProbe::DynamicVariableCopyProbe(const DynamicVariableCopyProbe& other) noexcept:
    m_counts(other.m_counts)
{
    ++m_counts->copies;
}

DynamicVariableCopyProbe::DynamicVariableCopyProbe(DynamicVariableCopyProbe&& other) noexcept:
    m_counts(other.m_counts)
{
    ++m_counts->moves;
}

void DynamicVariableCopyProbe::operator()(
    VirtualMachine* /*machine*/,
    const RuntimeValue& /*value*/
) const {
    ++m_counts->invocations;
}

void DynamicVariableCopyProbe::operator()(
    VirtualMachine* /*machine*/,
    RuntimeValue& value
) const {
    ++m_counts->invocations;
    value.setIntegerValue(0);
}

} // namespace nomad::test
