// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/ResourceManager.hpp>

#include <nomad/resource/Animation.hpp>
#include <nomad/resource/Font.hpp>
#include <nomad/resource/Sprite.hpp>
#include <nomad/resource/SpriteCatalog.hpp>
#include <nomad/resource/Texture.hpp>

#include <nomad/game/Canvas.hpp>
#include <nomad/game/Game.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/String.hpp>

#include <filesystem>
#include <iterator>
#include <set>

namespace nomad {

namespace {

void validateFrame(
    const PackedSpriteDefinition& sprite,
    const PackedSpritePage& page
) {
    if (
        sprite.source.getLeft() < 0 ||
        sprite.source.getTop() < 0 ||
        sprite.source.getWidth() <= 0 ||
        sprite.source.getHeight() <= 0 ||
        sprite.frame.getLeft() < 0 ||
        sprite.frame.getTop() < 0 ||
        sprite.frame.getWidth() <= 0 ||
        sprite.frame.getHeight() <= 0 ||
        sprite.source.getLeft() + sprite.frame.getWidth() > sprite.source.getWidth() ||
        sprite.source.getTop() + sprite.frame.getHeight() > sprite.source.getHeight() ||
        sprite.frame.getRight() > page.width ||
        sprite.frame.getBottom() > page.height
    ) {
        throw ResourceException("Packed sprite has invalid rectangles: '" + sprite.name + "'");
    }
}

} // namespace

ResourceManager::ResourceManager(Game* game, const NomadString& basePath) :
    m_game(game),
    m_animations(this),
    m_fonts(this),
    m_sprites(this),
    m_textures(this)
{
    if (basePath.empty() == false) {
        // Make sure base_path does not end with a path separator.
        if (basePath.back() == '\\' || basePath.back() == '/') {
            m_basePath = basePath.substr(0, basePath.size() - 1);
        } else {
            m_basePath = basePath;
        }
    }
}

ResourceManager::~ResourceManager() {
}

Game* ResourceManager::getGame() const {
    return m_game;
}

const NomadString& ResourceManager::getBasePath() const {
    return m_basePath;
}

NomadString ResourceManager::makeResourcePath(const NomadString& resourceName) const {
    auto file_name = m_basePath + "/" + resourceName;

    // Adjust path for Windows.
    if constexpr (std::filesystem::path::preferred_separator == '\\') {
        std::replace(file_name.begin(), file_name.end(), '/', '\\');
    }

    return file_name;
}

AnimationManager* ResourceManager::getAnimations() {
    return &m_animations;
}

FontManager* ResourceManager::getFonts() {
    return &m_fonts;
}

SpriteManager* ResourceManager::getSprites() {
    return &m_sprites;
}

TextManager* ResourceManager::getText() {
    return &m_text;
}

TextureManager* ResourceManager::getTextures() {
    return &m_textures;
}

NomadIndex ResourceManager::loadSpriteRoots(
    const std::vector<NomadPath>& directories,
    const bool directoriesExplicitlyConfigured,
    const NomadInteger maxTextureSize,
    const NomadInteger padding,
    const bool missingDirectoriesAllowed
) {
    std::vector<PackedSpriteGroup> groups;
    std::set<NomadString> groupNames;
    std::set<NomadString> spriteNames;
    NomadIndex spriteCount = 0;
    const auto resourcePath = pathFromUtf8(m_basePath);
    const auto directoryRequired =
        directoriesExplicitlyConfigured && !missingDirectoriesAllowed;

    for (const auto& directory : directories) {
        auto rootGroups = loadSpriteRoot(
            resourcePath,
            directory,
            directoryRequired,
            maxTextureSize,
            padding
        );

        for (const auto& group : rootGroups) {
            if (!groupNames.insert(stringToLowerAscii(group.name)).second) {
                throw SpriteCatalogError(
                    "Sprite group name collision across configured roots: '" + group.name + "'"
                );
            }

            spriteCount += group.sprites.size();

            for (const auto& sprite : group.sprites) {
                if (!spriteNames.insert(stringToLowerAscii(sprite.name)).second) {
                    throw SpriteCatalogError(
                        "Sprite resource name collision across configured roots: '" +
                        sprite.name + "'"
                    );
                }
            }
        }

        groups.insert(
            groups.end(),
            std::make_move_iterator(rootGroups.begin()),
            std::make_move_iterator(rootGroups.end())
        );
    }

    registerPackedSprites(groups);
    return spriteCount;
}

void ResourceManager::registerPackedSprites(const std::vector<PackedSpriteGroup>& groups) {
    std::set<NomadString> textureNames;
    std::set<NomadString> spriteNames;
    NomadIndex textureCount = 0;
    NomadIndex spriteCount = 0;

    for (NomadIndex index = 0; index < m_textures.getTextureCount(); ++index) {
        textureNames.insert(stringToLowerAscii(m_textures.getTexture(toNomadId(index))->getName()));
    }
    for (NomadIndex index = 0; index < m_sprites.getSpriteCount(); ++index) {
        spriteNames.insert(stringToLowerAscii(m_sprites.getSprite(toNomadId(index))->getName()));
    }

    for (const auto& group : groups) {
        if (group.name.empty()) {
            throw ResourceException("Packed sprite group name must not be empty");
        }

        textureCount += group.pages.size();
        spriteCount += group.sprites.size();

        for (NomadIndex pageIndex = 0; pageIndex < group.pages.size(); ++pageIndex) {
            const auto& page = group.pages[pageIndex];
            const auto textureName = makePackedSpritePageName(group.name, pageIndex);

            if (!textureNames.insert(stringToLowerAscii(textureName)).second) {
                throw ResourceException("Packed texture name is already registered: '" + textureName + "'");
            }
            if (
                !page.surface ||
                page.width <= 0 ||
                page.height <= 0 ||
                page.surface->w != page.width ||
                page.surface->h != page.height
            ) {
                throw ResourceException("Packed texture page is invalid: '" + textureName + "'");
            }
        }

        for (const auto& sprite : group.sprites) {
            if (sprite.name.empty()) {
                throw ResourceException("Packed sprite name must not be empty");
            }
            if (!spriteNames.insert(stringToLowerAscii(sprite.name)).second) {
                throw ResourceException("Packed sprite name is already registered: '" + sprite.name + "'");
            }
            if (sprite.page >= group.pages.size()) {
                throw ResourceException("Packed sprite references an invalid texture page: '" + sprite.name + "'");
            }

            validateFrame(sprite, group.pages[sprite.page]);
        }
    }

    std::vector<std::unique_ptr<Texture>> textures;
    std::vector<std::vector<const Texture*>> groupTextures;
    textures.reserve(textureCount);
    groupTextures.reserve(groups.size());
    auto* renderer = m_game->getCanvas()->getSdlRenderer();

    for (const auto& group : groups) {
        auto& pages = groupTextures.emplace_back();
        pages.reserve(group.pages.size());

        for (NomadIndex pageIndex = 0; pageIndex < group.pages.size(); ++pageIndex) {
            const auto& page = group.pages[pageIndex];
            const auto textureName = makePackedSpritePageName(group.name, pageIndex);
            auto* sdlTexture = SDL_CreateTextureFromSurface(renderer, page.surface.get());

            if (sdlTexture == nullptr) {
                throw ResourceException(
                    "Failed to create packed texture '" + textureName + "': " + SDL_GetError()
                );
            }

            auto texture = std::make_unique<Texture>(textureName, sdlTexture);
            pages.push_back(texture.get());
            textures.push_back(std::move(texture));
        }
    }

    std::vector<std::unique_ptr<Sprite>> sprites;
    sprites.reserve(spriteCount);

    for (NomadIndex groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        const auto& group = groups[groupIndex];

        for (const auto& sprite : group.sprites) {
            sprites.push_back(std::make_unique<Sprite>(
                sprite.name,
                groupTextures[groupIndex][sprite.page],
                sprite.source,
                sprite.frame
            ));
        }
    }

    m_textures.reserveAdditionalTextures(textures.size());
    m_sprites.reserveAdditionalSprites(sprites.size());
    m_textures.commitPreparedTextures(std::move(textures));
    m_sprites.commitPreparedSprites(std::move(sprites));
}

NomadString ResourceManager::makeResourceName(const NomadString& basePath, const NomadString& fileName) {
    return basePath + "/" + fileName;
}

} // nomad
