// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <FailingVariableContext.hpp>

#include <stdexcept>

namespace nomad::test {

FailingVariableContext::FailingVariableContext(const bool retainValueBeforeFailure):
    m_retainValueBeforeFailure(retainValueBeforeFailure)
{}

void FailingVariableContext::setValue(const VariableId variableId, const RuntimeValue& value) {
    if (m_retainValueBeforeFailure) {
        SimpleVariableContext::setValue(variableId, value);
    }

    throw std::runtime_error("context setter failed");
}

} // namespace nomad::test
