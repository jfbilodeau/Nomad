// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Canvas.hpp>

#include <nomad/game/Color.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/resource/ResourceManager.hpp>
#include <nomad/resource/Font.hpp>

#include <nomad/system/TempHeap.hpp>

#include <SDL3/SDL.h>

#include <utility>

namespace nomad {

///////////////////////////////////////////////////////////////////////////////
// Font
Font::Font(const NomadString& name, const NomadString& fileName, const NomadFloat pointSize):
    m_name(name)
{
    m_pointSize = pointSize;

    m_font = TTF_OpenFont(fileName.c_str(), static_cast<float>(pointSize));

    if (m_font == nullptr) {
        const auto error_message = SDL_GetError();

        throw ResourceException("Failed to load font: " + fileName + ". Reason: " + error_message);
    }
}

Font::~Font() {
    TTF_CloseFont(m_font);
}

const NomadString& Font::getName() const {
    return m_name;
}

NomadFloat Font::getPointSize() const {
    return m_pointSize;
}

TTF_Font* Font::getTtfFont() const {
    return m_font;
}

Texture* Font::generateTexture(
    const Canvas* canvas,
    const NomadString& text,
    const Color& color,
    const HorizontalAlignment alignment,
    const NomadInteger maxTextWidthPixels,
    const NomadInteger maxTextHeightPixels,
    const NomadFloat lineSpacing
) const {
    struct Line {
        TempString text;
        NomadInteger width;
    };

    if (text.empty()) {
        // Create a 1x1 texture
        return new Texture("", canvas->getGame(), 1, 1);
    }

    const auto fontHeight = getFontHeight();

    const auto lineHeight = static_cast<NomadFloat>(fontHeight) * lineSpacing;

    SDL_Surface* surface;

    if (maxTextWidthPixels == 0) {
        surface = TTF_RenderText_Blended_Wrapped(m_font, text.c_str(), text.size(), color.toSdlColor(), 0);
    } else {
        // Pre-allocate strings to avoid allocations at each iteration.
        auto testLine = createTempString();
        auto currentLine = createTempString();
        NomadInteger longestLineWidth = 0;

        auto lines = createTempVector<Line>();

        auto splitText = createTempStringVector();
        splitLines(createTempString(text), splitText);

        for (const auto& line : splitText) {
            const auto currentTextHeight = static_cast<NomadInteger>(static_cast<NomadFloat>(lines.size()) * lineHeight);

            if (maxTextHeightPixels != 0 && currentTextHeight > maxTextHeightPixels) {
                break;
            }

            auto words = createTempStringVector();
            split(line, " ", words);

            for (const auto& word : words) {
                testLine.clear();
                testLine.append(currentLine).append(currentLine.empty() ? "" : " ").append(word);
                auto lineWidth = getTextWidth(testLine.c_str());

                if (lineWidth > maxTextWidthPixels) {
                    if (!currentLine.empty()) {
                        lines.emplace_back(
                            Line{
                                currentLine,
                                getTextWidth(currentLine.c_str())
                            }
                        );
                        currentLine = word; // Start next line with word
                    } else {
                        // Word is too long to fit on a line.
                        lines.emplace_back(
                            Line{
                                word,
                                getTextWidth(word.c_str())
                            }
                        );
                    }

                    lineWidth = getTextWidth(lines.back().text.c_str());
                    longestLineWidth = std::max(longestLineWidth, lineWidth);
                } else {
                    currentLine = testLine;
                }
            }

            if (!currentLine.empty()) {
                lines.emplace_back(
                    Line {
                        currentLine,
                        getTextWidth(currentLine.c_str())
                    }
                );
                longestLineWidth = std::max(longestLineWidth, getTextWidth(currentLine.c_str()));
            }
        }

        const auto currentTextHeight = static_cast<NomadInteger>(static_cast<NomadFloat>(lines.size()) * lineHeight);

        auto surfaceHeight = maxTextHeightPixels;
        if (surfaceHeight == 0) {

            surfaceHeight = currentTextHeight;
        }

        auto surfaceWidth = maxTextWidthPixels;
        if (surfaceWidth == 0) {

            surfaceWidth = longestLineWidth;
        }

        surface = SDL_CreateSurface(

            static_cast<int>(surfaceWidth),
            static_cast<int>(surfaceHeight),
            SDL_PIXELFORMAT_RGBA4444
        );

        for (NomadIndex lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {

            const auto& [lineText, width] = lines[lineIndex];
            const auto textSurface = TTF_RenderText_Blended(m_font, lineText.c_str(), lineText.size(), color.toSdlColor());

            const auto textY = static_cast<int>(std::lround(static_cast<NomadFloat>(lineIndex) * lineHeight));
            SDL_Rect destinationRect;

            switch (alignment) {
            case HorizontalAlignment::Left:
                destinationRect = SDL_Rect{
                    .x = 0,
                    .y = textY,
                    .w = textSurface->w,
                    .h = textSurface->h
                };
                break;

            case HorizontalAlignment::Middle:
                destinationRect = SDL_Rect{
                    static_cast<int>((surfaceWidth - width) / 2),
                    textY,
                    textSurface->w,
                    textSurface->h
                };
                break;

            case HorizontalAlignment::Right:
                destinationRect = SDL_Rect{
                    0,
                    textY,
                    textSurface->w,
                    textSurface->h
                };
                break;
            }

            SDL_BlitSurface(textSurface, nullptr, surface, &destinationRect);

            SDL_DestroySurface(textSurface);
        }
    }

    auto texture = SDL_CreateTextureFromSurface(canvas->getSdlRenderer(), surface);

    if (texture == nullptr) {
        const NomadString error_message = SDL_GetError();

        log::error("Failed to create texture from surface. Reason: " + error_message);
    }

    SDL_DestroySurface(surface);

    return new Texture(text, texture);
}

NomadInteger Font::getTextWidth(const NomadChar* text) const {
    int width = 0;

    TTF_GetStringSize(m_font, text, std::strlen(text), &width, nullptr);

    return width;
}

NomadInteger Font::getTextWidth(const NomadString& text) const {
    return getTextWidth(text.c_str());
}

NomadInteger Font::getTextHeight(const NomadString& text) const {
    int height = 0;

    TTF_GetStringSize(m_font, text.c_str(), text.size(), nullptr, &height);

    return height;
}

NomadInteger Font::getFontHeight() const {
    const auto font_height = TTF_GetFontHeight(m_font);

    return font_height;
}

///////////////////////////////////////////////////////////////////////////////
// FontManager
FontManager::FontManager(ResourceManager* resources):
    m_resources(resources)
{}

NomadId FontManager::registerFont(const NomadString& fontName, NomadFloat pointSize) {
    const auto fontId = toNomadId(m_fonts.size());

    const auto fileName = m_resources->makeResourcePath(fontName);

    m_fonts.emplace_back(
        std::make_unique<Font>(
            fontName,
            fileName,
            pointSize
        )
    );

    return fontId;
}

const Font* FontManager::getFont(const NomadId fontId) const {
    const auto fontIndex = toNomadIndex(fontId);

    if (fontIndex >= m_fonts.size()) {
        return nullptr;
    }

    return m_fonts[fontIndex].get();
}

const Font* FontManager::getFontByName(const NomadString& fontName) const {
    for (const auto& font : m_fonts) {
        if (font->getName() == fontName) {
            return font.get();
        }
    }

    return nullptr;
}

} // nomand
