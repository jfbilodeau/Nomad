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

    [[nodiscard]] FontObject* registerFont(const NomadString& fontName, NomadFloat pointSize);

private:
    friend class FontObject;
    void unregisterFont(FontObject* font);
    ResourceManager* m_resources;
    // Non-owning live instances, used only to invalidate resources at shutdown.
    std::vector<FontObject*> m_fonts;
};

} // nomad
