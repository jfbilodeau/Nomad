// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NomadStringObject.hpp>

#include <atomic>
#include <utility>

namespace nomad {

namespace {

#ifdef NOMAD_DEBUG
std::atomic<NomadInteger> s_liveCount{0};
#endif

} // namespace

NomadStringObject::NomadStringObject(NomadString text, const bool counted):
    m_text(std::move(text)),
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

NomadStringObject* NomadStringObject::createEmpty() {
    // Intentionally never released: the shared empty string lives for the whole process.
    static auto* const emptyString = [] {
        return new NomadStringObject(NomadString(), false);
    }();

    emptyString->addRef();

    return emptyString;
}

NomadStringObject* NomadStringObject::create(const NomadStringView text) {
    if (text.empty()) {
        return createEmpty();
    }

    return new NomadStringObject(NomadString(text), true);
}

NomadStringObject* NomadStringObject::create(const NomadChar* text) {
    return create(NomadStringView(text));
}

NomadStringObject* NomadStringObject::create(const NomadString& text) {
    return create(NomadStringView(text));
}

NomadInteger NomadStringObject::getLiveCount() noexcept {
#ifdef NOMAD_DEBUG
    return s_liveCount.load();
#else
    return 0;
#endif
}

} // namespace nomad
