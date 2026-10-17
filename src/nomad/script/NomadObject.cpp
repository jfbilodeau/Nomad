// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NomadObject.hpp>

#include <atomic>
#include <exception>

namespace nomad {

namespace {

std::atomic<NomadIndex> liveObjectCount{0};

} // namespace

NomadObject::NomadObject() noexcept {
    liveObjectCount.fetch_add(1, std::memory_order_relaxed);
}

NomadObject::~NomadObject() {
    liveObjectCount.fetch_sub(1, std::memory_order_relaxed);
}

NomadIndex NomadObject::getLiveObjectCount() noexcept {
    return liveObjectCount.load(std::memory_order_relaxed);
}

void NomadObject::addRef() noexcept {
    m_referenceCount.fetch_add(1, std::memory_order_relaxed);
}

void NomadObject::releaseRef() noexcept {
    const auto previous = m_referenceCount.fetch_sub(1, std::memory_order_acq_rel);
    if (previous == 0) {
        std::terminate();
    }
    if (previous == 1) {
        delete this;
    }
}

NomadIndex NomadObject::getReferenceCount() const noexcept {
    return m_referenceCount.load(std::memory_order_relaxed);
}

} // namespace nomad
