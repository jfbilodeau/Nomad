// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <atomic>

namespace nomad {

class NomadObject {
public:
    NomadObject() noexcept;
    NomadObject(const NomadObject&) = delete;
    NomadObject& operator=(const NomadObject&) = delete;

    void addRef() noexcept;
    void releaseRef() noexcept;

    [[nodiscard]] NomadIndex getReferenceCount() const noexcept;

    // Number of objects currently alive (all types).
    [[nodiscard]] static NomadIndex getLiveObjectCount() noexcept;

protected:
    virtual ~NomadObject();

private:
    std::atomic<NomadIndex> m_referenceCount{1};
};

} // namespace nomad
