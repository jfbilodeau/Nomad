// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

class FontObject;
class ResourceManager;

///////////////////////////////////////////////////////////////////////////////
// FontManager
///////////////////////////////////////////////////////////////////////////////
class FontManager {
public:
    explicit FontManager(ResourceManager* resources);
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;
    ~FontManager();

    [[nodiscard]] NomadId registerFont(const NomadString& fontName, NomadFloat pointSize);
    [[nodiscard]] FontObject* getFont(NomadId fontId) const;
    [[nodiscard]] const FontObject* getFontByName(const NomadString& fontName) const;

private:
    friend class FontObject;
    void unregisterFont(FontObject* font);
    ResourceManager* m_resources;
    std::vector<FontObject*> m_fonts;
};

} // nomad
