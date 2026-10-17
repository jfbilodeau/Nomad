// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>

namespace nomad {

class NomadStringObject final : public NomadObject {
public:
    // The returned object has one reference. Empty strings share a single instance.
    [[nodiscard]] static NomadStringObject* create(NomadStringView text);
    [[nodiscard]] static NomadStringObject* create(const NomadChar* text);
    [[nodiscard]] static NomadStringObject* create(const NomadString& text);

    // Returns a new reference to the shared empty string.
    [[nodiscard]] static NomadStringObject* createEmpty();

    [[nodiscard]] const NomadChar* getText() const noexcept { return m_text.c_str(); }
    [[nodiscard]] NomadIndex getLength() const noexcept { return m_text.size(); }
    [[nodiscard]] NomadStringView getView() const noexcept { return m_text; }
    [[nodiscard]] const NomadString& getString() const noexcept { return m_text; }

    // Number of non-empty string objects currently alive. Only tracked in debug builds; always 0 otherwise.
    [[nodiscard]] static NomadInteger getLiveCount() noexcept;

private:
    NomadStringObject(NomadString text, bool counted);
    ~NomadStringObject() override;

    NomadString m_text;
    bool m_counted;
};

} // namespace nomad
