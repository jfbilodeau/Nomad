// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/IteratorObject.hpp>

namespace nomad {

class Array1dObject;

class ArrayIterator final : public IteratorObject {
public:
    explicit ArrayIterator(Array1dObject* array);
    ~ArrayIterator() override;

protected:
    [[nodiscard]] bool onMoveNext(RuntimeValue& value) override;

private:
    Array1dObject* m_array;
    NomadIndex m_next = 0;
};

} // namespace nomad
