// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/game/Alignment.hpp>
#include <nomad/game/Color.hpp>

#include <nomad/script/NomadObject.hpp>

#include <SDL3_ttf/SDL_ttf.h>

namespace nomad {

class Canvas;
class FontManager;
class Texture;

class FontObject final : public NomadObject {
public:
    FontObject(const NomadString& name, const NomadString& fileName, NomadFloat pointSize);
    FontObject(const FontObject&) = delete;
    FontObject& operator=(const FontObject&) = delete;
    ~FontObject() override;

    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] NomadFloat getPointSize() const;
    [[nodiscard]] TTF_Font* getTtfFont() const;

    [[nodiscard]] Texture* generateTexture(
        const Canvas* canvas,
        const NomadString& text,
        const Color& color,
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
    friend class FontManager;

    void detachManager();

    FontManager* m_manager = nullptr;
    NomadString m_name;
    TTF_Font* m_font = nullptr;
    NomadFloat m_pointSize = 0;
};

} // namespace nomad
