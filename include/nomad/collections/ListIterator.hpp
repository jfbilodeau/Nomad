// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/IteratorObject.hpp>

#include <cstdint>

namespace nomad {

class ListObject;

class ListIterator final : public IteratorObject {
public:
    explicit ListIterator(ListObject* list);
    ~ListIterator() override;

protected:
    [[nodiscard]] bool onMoveNext(RuntimeValue& value) override;

private:
    ListObject* m_list;
    std::uint64_t m_version;
    NomadIndex m_next = 0;
};

} // namespace nomad
