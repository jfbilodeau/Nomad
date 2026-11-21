// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/VariableContext.hpp>

namespace nomad::test {

class FailingVariableContext final : public SimpleVariableContext {
public:
    explicit FailingVariableContext(bool retainValueBeforeFailure);

    void setValue(VariableId variableId, const RuntimeValue& value) override;

private:
    bool m_retainValueBeforeFailure;
};

} // namespace nomad::test
