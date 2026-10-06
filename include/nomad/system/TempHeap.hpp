// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <array>
#include <memory_resource>
#include <string>
#include <vector>

namespace nomad {


using TempString = std::pmr::basic_string<NomadString::value_type, std::char_traits<NomadString::value_type>>;
template<typename T>
using TempVector = std::vector<T, std::pmr::polymorphic_allocator<T>>;
using TempStringVector = TempVector<TempString>;

std::pmr::memory_resource* getTempBuffer();

// Bytes used since the last reset; allocations are not individually reclaimed.
[[nodiscard]] std::size_t getTempHeapSize();
[[nodiscard]] std::size_t getTempHeapMaxSize();

// Reuse the storage at the frame boundary, after all temporary objects are destroyed.
void resetTempHeap();

template<typename T>
TempVector<T> createTempVector() {
    return TempVector<T>(getTempBuffer());
}

template<typename T>
TempVector<T> createTempVector(size_t size) {
    return TempVector<T>(size, getTempBuffer());
}

template<typename T>
TempVector<T> createTempVector(size_t size, const T& value) {
    return TempVector<T>(size, value, getTempBuffer());
}

template<typename T>
TempVector<T> createTempVector(std::vector<T>& vector) {
    return TempVector<T>(vector.begin(), vector.end(), getTempBuffer());
}

template<typename T>
TempVector<T> createTempVector(const std::vector<T>& vector) {
    return TempVector<T>(vector.begin(), vector.end(), getTempBuffer());
}

inline TempString createTempString() {
    return TempString(getTempBuffer());
}

inline TempString createTempString(const NomadString& string) {
    return TempString(string, getTempBuffer());
}

inline TempString createTempString(const NomadChar* string) {
    return { string, getTempBuffer() };
}

inline TempStringVector createTempStringVector() {
    return TempStringVector(getTempBuffer());
}

inline TempStringVector createTempStringVector(const std::vector<NomadString>& strings) {
    return { strings.begin(), strings.end(), getTempBuffer() };
}

} // namespace nomad
