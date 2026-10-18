// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/resource/Font.hpp>

#include <nomad/game/resource/FontObject.hpp>
#include <nomad/game/resource/ResourceManager.hpp>

#include <algorithm>
#include <memory>

namespace nomad {

FontManager::FontManager(ResourceManager* resources):
    m_resources(resources)
{}

FontManager::~FontManager() {
    for (auto* font : m_fonts) {
        if (font != nullptr) {
            font->detachManager();
        }
    }
}

void FontManager::unregisterFont(FontObject* font) {
    const auto found = std::ranges::find(m_fonts, font);
    if (found != m_fonts.end()) {
        *found = nullptr;
    }
}

NomadId FontManager::registerFont(const NomadString& fontName, const NomadFloat pointSize) {
    const auto fontId = toNomadId(m_fonts.size());
    const auto fileName = m_resources->makeResourcePath(fontName);
    auto font = std::make_unique<FontObject>(fontName, fileName, pointSize);
    m_fonts.push_back(font.get());
    font->m_manager = this;
    font.release();
    return fontId;
}

FontObject* FontManager::getFont(const NomadId fontId) const {
    const auto fontIndex = toNomadIndex(fontId);
    return fontIndex < m_fonts.size() ? m_fonts[fontIndex] : nullptr;
}

const FontObject* FontManager::getFontByName(const NomadString& fontName) const {
    for (const auto* font : m_fonts) {
        if (font != nullptr && font->getName() == fontName) {
            return font;
        }
    }
    return nullptr;
}

} // namespace nomad
