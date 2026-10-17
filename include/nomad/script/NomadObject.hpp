// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <atomic>

namespace nomad {

class Runtime;

class NomadObject {
public:
    explicit NomadObject(Runtime& runtime);
    NomadObject(const NomadObject&) = delete;
    NomadObject& operator=(const NomadObject&) = delete;

    void addRef() noexcept;
    void releaseRef() noexcept;

    [[nodiscard]] Runtime& getRuntime() const noexcept;
    [[nodiscard]] NomadIndex getReferenceCount() const noexcept;

protected:
    virtual ~NomadObject();
    [[nodiscard]] virtual bool reclaimStep() noexcept;
    virtual void detachReferences() noexcept;

private:
    friend class Runtime;

    Runtime& m_runtime;
    std::atomic<NomadIndex> m_referenceCount{1};
    NomadObject* m_previousObject = nullptr;
    NomadObject* m_nextObject = nullptr;
    NomadObject* m_nextReclamation = nullptr;
    bool m_queued = false;
};

} // namespace nomad
