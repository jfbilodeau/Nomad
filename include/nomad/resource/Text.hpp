// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <unordered_map>
#include <vector>

namespace nomad {

// Forward declarations
class Game;

class TextManager {
public:
    TextManager() = default;

    ~TextManager() = default;

    void loadTextFromCsv(const Game* game, const NomadString& path);

    [[nodiscard]]
    bool hasLanguage(const NomadString& languageCode) const;
    [[nodiscard]]
    const std::vector<NomadString>& getLanguageCodes() const;

    [[nodiscard]]
    bool hasText(const NomadString& languageCode, const NomadString& key) const;
    NomadString& getText(const NomadString& languageCode, const NomadString& key, NomadString& text) const;

    void getAllText(const NomadString& languageCode, std::unordered_map<NomadString, NomadString>& texts) const;

private:
    std::vector<NomadString> m_languageCodes;
    std::unordered_map<NomadString, std::unordered_map<NomadString, NomadString>> m_texts;
};

} // namespace nomad
