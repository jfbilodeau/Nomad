// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NomadObject.hpp>

#include <nomad/script/Runtime.hpp>

#include <exception>
#include <limits>

namespace nomad {

NomadObject::NomadObject(Runtime& runtime):
    m_runtime(runtime)
{
    m_runtime.registerObject(this);
}

NomadObject::~NomadObject() {
    m_runtime.unregisterObject(this);
}

void NomadObject::addRef() noexcept {
    const auto previous = m_referenceCount.fetch_add(1, std::memory_order_relaxed);
    if (previous == 0 || previous == std::numeric_limits<NomadIndex>::max()) {
        std::terminate();
    }
}

void NomadObject::releaseRef() noexcept {
    if (m_runtime.m_destroyingObjects) {
        return;
    }

    const auto previous = m_referenceCount.fetch_sub(1, std::memory_order_acq_rel);
    if (previous == 0) {
        std::terminate();
    }
    if (previous == 1) {
        m_runtime.reclaimObject(this);
    }
}

Runtime& NomadObject::getRuntime() const noexcept {
    return m_runtime;
}

NomadIndex NomadObject::getReferenceCount() const noexcept {
    return m_referenceCount.load(std::memory_order_relaxed);
}

bool NomadObject::reclaimStep() noexcept {
    return true;
}

void NomadObject::detachReferences() noexcept {
}

} // namespace nomad
