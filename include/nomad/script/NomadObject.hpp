// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <atomic>
#include <concepts>
#include <type_traits>

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

inline void addRef(NomadObject* object) noexcept {
    if (object != nullptr) {
        object->addRef();
    }
}

inline void releaseRef(NomadObject* object) noexcept {
    if (object != nullptr) {
        object->releaseRef();
    }
}

template<std::derived_from<NomadObject> Object>
[[nodiscard]] inline Object* replace(Object* oldObject, std::type_identity_t<Object>* newObject) noexcept {
    if (oldObject != newObject) {
        addRef(newObject);
        releaseRef(oldObject);
    }
    return newObject;
}

} // namespace nomad
