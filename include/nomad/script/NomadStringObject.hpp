// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>

#include <cstddef>

namespace nomad {

class NomadStringObject final : public NomadObject {
public:
    // The returned object has one reference. Empty strings share a single instance.
    [[nodiscard]] static NomadStringObject* create(const NomadChar* text);

    // `text` must point to the characters of a `NomadStringObject`.
    [[nodiscard]] static NomadStringObject* fromText(const NomadChar* text) noexcept;

    [[nodiscard]] const NomadChar* getText() const noexcept;

    // Number of non-empty string objects currently alive. Only tracked in debug builds; always 0 otherwise.
    [[nodiscard]] static NomadInteger getLiveCount() noexcept;

    static void operator delete(void* memory) noexcept;

private:
    explicit NomadStringObject(bool counted) noexcept;
    ~NomadStringObject() override;

    bool m_counted;
};

} // namespace nomad
