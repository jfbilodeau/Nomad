// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/Memory.hpp>

#include <cstdlib>

namespace nomad {

class HeapAllocator final : public Allocator {
public:
    void* allocate(std::size_t size) override {
        return std::malloc(size);
    }

    void deallocate(void* ptr) noexcept override {
        std::free(ptr);
    }
};


Allocator * getDefaultAllocator() {
    static HeapAllocator default_allocator;

    return &default_allocator;
}

} // namespace nomad
