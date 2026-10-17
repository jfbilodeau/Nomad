// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/resource/Text.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/game/resource/ResourceManager.hpp>

#include <boost/tokenizer.hpp>
#include <boost/algorithm/string/trim.hpp>

#include <algorithm>
#include <fstream>
#include <filesystem>



namespace nomad {

void TextManager::clear() {
    m_languageCodes.clear();
    m_texts.clear();
}

void TextManager::loadTextFromCsv(const Game* game, const NomadString& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        NomadString message = "Could not open text file: " + path;
        throw ResourceException(message);
    }

    NomadString line;

    if (!std::getline(file, line)) {
        throw ResourceException("Could not read header (first) line from text file: " + path);
    }

    boost::trim(line);

    std::vector<NomadString> languageCodes;

    { // Read headers
        boost::tokenizer<boost::escaped_list_separator<NomadChar>> tokens(line);

        for (const auto& token: tokens) {
            if (token == "key") {
                // Skip over 'key' column
                continue;
            }

            languageCodes.push_back(token);

            if (std::ranges::find(m_languageCodes, token) == m_languageCodes.end()) {
                m_languageCodes.push_back(token);
            }
        }
    }

    while (std::getline(file, line)) {
        boost::trim(line);

        boost::tokenizer<boost::escaped_list_separator<char>> tokens(line);

        NomadString key;
        auto columnIndex = -1;

        for (const auto& token : tokens) {
            columnIndex++;

            if (columnIndex == 0) {
                key = token;
                continue;
            }

            const auto& languageCode = languageCodes.at(columnIndex - 1);

            m_texts[languageCode][key] = token;

            if (token == "#") {
                auto textFileName = std::format("text/{}-{}.txt", key, languageCode);
                const auto fileName = game->makeResourcePath(textFileName);

                if (std::filesystem::exists(fileName)) {
                    if (std::ifstream fileStream(fileName); fileStream.is_open()) {
                        std::stringstream buffer;
                        buffer << fileStream.rdbuf();
                        m_texts[languageCode][key] = buffer.str();
                    }
                }
            }
        }
    }
}

bool TextManager::hasLanguage(const NomadString& languageCode) const {
    return m_texts.find(languageCode) != m_texts.end();
}

const std::vector<NomadString>& TextManager::getLanguageCodes() const {
    return m_languageCodes;
}

bool TextManager::hasText(const NomadString& languageCode, const NomadString& key) const {
    return
        m_texts.find(languageCode) != m_texts.end() &&
        m_texts.at(languageCode).find(key) != m_texts.at(languageCode).end();
}

NomadString& TextManager::getText(const NomadString& languageCode, const NomadString& key, NomadString& text) const {
    if (hasText(languageCode, key)) {
        text = m_texts.at(languageCode).at(key);
    } else {
        text = "[" + languageCode + ":" + key + "]";
    }

    return text;
}

void TextManager::getAllText(const NomadString& languageCode, std::unordered_map<NomadString, NomadString>& texts) const {
    texts = m_texts.at(languageCode);
}

} // namespace nomad
