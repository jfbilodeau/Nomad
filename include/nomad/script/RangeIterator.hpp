// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/IteratorObject.hpp>

namespace nomad {

class RangeObject;

class RangeIterator final : public IteratorObject {
public:
    RangeIterator(
        const Type* valueType,
        const RangeObject& range
    );

protected:
    [[nodiscard]] bool onMoveNext(RuntimeValue& value) override;

private:
    NomadInteger m_next;
    NomadInteger m_stop;
    NomadInteger m_step;
    bool m_exhausted = false;
};

} // namespace nomad
