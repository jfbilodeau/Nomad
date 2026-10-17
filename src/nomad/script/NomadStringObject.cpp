// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NomadStringObject.hpp>

#include <atomic>
#include <cstring>
#include <new>

namespace nomad {

namespace {

#ifdef NOMAD_DEBUG
std::atomic<NomadInteger> s_liveCount{0};
#endif

} // namespace

NomadStringObject::NomadStringObject(const bool counted) noexcept:
    m_counted(counted)
{
#ifdef NOMAD_DEBUG
    if (m_counted) {
        ++s_liveCount;
    }
#endif
}

NomadStringObject::~NomadStringObject() {
#ifdef NOMAD_DEBUG
    if (m_counted) {
        --s_liveCount;
    }
#endif
}

NomadStringObject* NomadStringObject::create(const NomadChar* text) {
    const auto length = std::strlen(text);

    if (length == 0) {
        // Intentionally never released: the shared empty string lives for the whole process.
        static auto* const emptyString = [] {
            auto* memory = ::operator new(sizeof(NomadStringObject) + 1);
            auto* object = new(memory) NomadStringObject(false);
            *reinterpret_cast<NomadChar*>(object + 1) = '\0';

            return object;
        }();

        emptyString->addRef();

        return emptyString;
    }

    auto* memory = ::operator new(sizeof(NomadStringObject) + length + 1);
    auto* object = new(memory) NomadStringObject(true);
    std::memcpy(object + 1, text, length + 1); // +1 to copy the null terminator

    return object;
}

NomadStringObject* NomadStringObject::fromText(const NomadChar* text) noexcept {
    return const_cast<NomadStringObject*>(reinterpret_cast<const NomadStringObject*>(text)) - 1;
}

const NomadChar* NomadStringObject::getText() const noexcept {
    return reinterpret_cast<const NomadChar*>(this + 1);
}

NomadInteger NomadStringObject::getLiveCount() noexcept {
#ifdef NOMAD_DEBUG
    return s_liveCount.load();
#else
    return 0;
#endif
}

void NomadStringObject::operator delete(void* memory) noexcept {
    ::operator delete(memory);
}

} // namespace nomad
