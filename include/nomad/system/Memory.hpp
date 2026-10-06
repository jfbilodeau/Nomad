// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <cstddef>

namespace nomad {

class Allocator {
public:
    virtual ~Allocator() = default;

    [[nodiscard]] virtual void* allocate(std::size_t size) = 0;
    virtual void deallocate(void* ptr) noexcept = 0;
};

Allocator* getDefaultAllocator();

} // namespace nomad
