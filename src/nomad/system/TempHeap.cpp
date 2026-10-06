// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/TempHeap.hpp>

#include <array>

namespace nomad {

namespace {

constexpr std::size_t TEMP_HEAP_SIZE = 1024 * 1024 * 10; // 10 MB

class NomadMemoryResource final : public std::pmr::memory_resource {
public:
    [[nodiscard]]
    std::size_t get_allocated() const;

    void release();

    [[nodiscard]]
    std::size_t getMaxHeapSize() const;

protected:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;

    void do_deallocate(void*, std::size_t, std::size_t) override;

    [[nodiscard]]
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

private:
    std::array<std::byte, TEMP_HEAP_SIZE> m_buffer = {};
    std::size_t m_allocated = 0;
};

NomadMemoryResource tempBuffer;

// inline std::array<std::byte, TEMP_HEAP_SIZE> tempHeapBuffer;
// inline std::pmr::monotonic_buffer_resource tempBuffer{tempHeapBuffer.data(), tempHeapBuffer.size()};

}

std::size_t NomadMemoryResource::get_allocated() const {
    return m_allocated;
}

void NomadMemoryResource::release() {
    m_allocated = 0;
}

std::size_t NomadMemoryResource::getMaxHeapSize() const {
    return m_buffer.size();
}

void* NomadMemoryResource::do_allocate(const std::size_t bytes, const std::size_t alignment) {
    std::byte* current = m_buffer.data() + m_allocated;
    std::size_t space = m_buffer.size() - m_allocated;

    // Check for buffer overflow before attempting alignment
    if (bytes > space) {
        throw std::bad_alloc();
    }

    void* aligned = std::align(alignment, bytes, reinterpret_cast<void*&>(current), space);
    if (!aligned) {
        throw std::bad_alloc();
    }

    m_allocated = static_cast<std::size_t>(static_cast<std::byte*>(aligned) - m_buffer.data()) + bytes;

    return aligned;
}

void NomadMemoryResource::do_deallocate(void *, std::size_t, std::size_t) {
    // Do nothing
}

bool NomadMemoryResource::do_is_equal(const std::pmr::memory_resource &other) const noexcept {
    return this == &other;
}

std::pmr::memory_resource* getTempBuffer() {
    return &tempBuffer;
}

std::size_t getTempHeapSize() {
    return tempBuffer.get_allocated();
}

std::size_t getTempHeapMaxSize() {
    return tempBuffer.getMaxHeapSize();
}

void resetTempHeap() {
    tempBuffer.release();
}

} // namespace nomad
