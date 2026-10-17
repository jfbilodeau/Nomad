// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <atomic>

namespace nomad {

class NomadObject {
public:
    NomadObject() = default;
    NomadObject(const NomadObject&) = delete;
    NomadObject& operator=(const NomadObject&) = delete;

    void addRef() noexcept;
    void releaseRef() noexcept;

    [[nodiscard]] NomadIndex getReferenceCount() const noexcept;

protected:
    virtual ~NomadObject() = default;

private:
    std::atomic<NomadIndex> m_referenceCount{1};
};

} // namespace nomad
