// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/IteratorObject.hpp>

namespace nomad::test {

class FailingIteratorObject final : public IteratorObject {
public:
    FailingIteratorObject(const Type* valueType, const RuntimeValue& source);

    [[nodiscard]] NomadIndex getCallCount() const;

protected:
    [[nodiscard]] bool onMoveNext(RuntimeValue& value) override;

private:
    const Type* m_valueType;
    const RuntimeValue& m_source;
    NomadIndex m_callCount = 0;
};

} // namespace nomad::test
