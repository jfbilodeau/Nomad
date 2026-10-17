// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/game/Alignment.hpp>
#include <nomad/game/Color.hpp>

#include "SDL3_ttf/SDL_ttf.h"

namespace nomad {

// Forward declarations
class Canvas;
class Resource;
class Texture;

///////////////////////////////////////////////////////////////////////////////
// Font
///////////////////////////////////////////////////////////////////////////////
class Font {
public:
    explicit Font(const NomadString& name, const NomadString& fileName, NomadFloat pointSize);
    Font(const Font& other) = delete;
    ~Font();

    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] NomadFloat getPointSize() const;
    [[nodiscard]] TTF_Font* getTtfFont() const;

    Texture* generateTexture(
        const Canvas *canvas,
        const NomadString &text,
        const Color &color,
        HorizontalAlignment alignment,
        NomadInteger maxTextWidthPixels,
        NomadInteger maxTextHeightPixels,
        NomadFloat lineSpacing
    ) const;

    [[nodiscard]] NomadInteger getTextWidth(const NomadChar* text) const;
    [[nodiscard]] NomadInteger getTextWidth(const NomadString& text) const;
    [[nodiscard]] NomadInteger getTextHeight(const NomadString& text) const;
    [[nodiscard]] NomadInteger getFontHeight() const;

private:
    NomadString m_name;
    TTF_Font* m_font = nullptr;
    NomadFloat m_pointSize = 0;
};

///////////////////////////////////////////////////////////////////////////////
// FontManager
///////////////////////////////////////////////////////////////////////////////
class FontManager {
public:
    explicit FontManager(ResourceManager* resources);

    [[nodiscard]] NomadId registerFont(const NomadString& fontName, NomadFloat pointSize);
    [[nodiscard]] const Font* getFont(NomadId fontId) const;
    [[nodiscard]] const Font* getFontByName(const NomadString& fontName) const;

private:
    ResourceManager* m_resources;
    std::vector<std::unique_ptr<Font>> m_fonts;
};

} // nomad
