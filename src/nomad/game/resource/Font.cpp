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
        font->detachManager();
    }
}

void FontManager::unregisterFont(FontObject* font) {
    std::erase(m_fonts, font);
}

FontObject* FontManager::registerFont(const NomadString& fontName, const NomadFloat pointSize) {
    const auto fileName = m_resources->makeResourcePath(fontName);
    auto font = std::make_unique<FontObject>(fontName, fileName, pointSize);
    m_fonts.push_back(font.get());
    font->m_manager = this;
    return font.release();
}

} // namespace nomad
