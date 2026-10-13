// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/ResourceManager.hpp>

#include <nomad/resource/Animation.hpp>
#include <nomad/resource/Font.hpp>
#include <nomad/resource/Sprite.hpp>
#include <nomad/resource/SpriteAtlas.hpp>
#include <nomad/resource/Texture.hpp>

#include <nomad/game/Canvas.hpp>
#include <nomad/game/Game.hpp>

#include <boost/json.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>

namespace nomad {

namespace {

NomadString toLowerAscii(NomadString value) {
    for (auto& character : value) {
        character = static_cast<NomadChar>(std::tolower(static_cast<unsigned char>(character)));
    }

    return value;
}

NomadString makePackedTextureName(const NomadString& group, const NomadIndex page) {
    return group + "#" + std::to_string(page);
}

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

void ResourceManager::loadSpriteAtlas(const NomadString& resourceName) {
    try {
        log::debug("Loading sprite atlas: '" + resourceName + "'");

        NomadString path = makeResourcePath(resourceName);

        std::ifstream file(path);

        if (!file.is_open()) {
            throw ResourceException("Cloud not open file: '" + resourceName + "'");
        }

        boost::json::stream_parser parser;

        char buffer[1024];

        while (!file.eof()) {
            file.read(buffer, sizeof(buffer));
            parser.write(buffer, file.gcount());
        }

        auto json = parser.release().as_object();

        // Texture name is the resource name but with the extension replaced by .png
        NomadString texture_name = resourceName.substr(0, resourceName.find_last_of('.')) + ".png";

        auto texture_id = getTextures()->registerTexture(texture_name);
        auto texture = getTextures()->getTexture(texture_id);

        auto sprites = json.at("sprites").as_array();

        for (auto sprite: sprites) {
            NomadString name = sprite.at("name").as_string().c_str();

            Rectangle source(
                static_cast<int>(sprite.at("source").at("x").as_int64()),
                static_cast<int>(sprite.at("source").at("y").as_int64()),
                static_cast<int>(sprite.at("source").at("width").as_int64()),
                static_cast<int>(sprite.at("source").at("height").as_int64())
            );

            Rectangle frame(
                static_cast<int>(sprite.at("frame").at("x").as_int64()),
                static_cast<int>(sprite.at("frame").at("y").as_int64()),
                static_cast<int>(sprite.at("frame").at("width").as_int64()),
                static_cast<int>(sprite.at("frame").at("height").as_int64())
            );

            log::debug("Loading sprite: '" + name + "'");

            auto sprite_id = m_sprites.registerSprite(name, texture, source, frame);

            if (sprite_id == NOMAD_INVALID_ID) {
                throw ResourceException("Internal error: Failed to register sprite: '" + name + "'");
            }
        }

        log::debug("Loading texture: '" + texture_name + "'");
    }
    catch (const boost::exception& /*e*/) {
        throw ResourceException("Failed to load sprite atlas: '" + resourceName + "'");
    }
    catch (const std::exception& e) {
        throw ResourceException("Failed to load sprite atlas: '" + resourceName + "' - " + e.what());
    }
}

void ResourceManager::registerPackedSprites(const std::vector<PackedSpriteGroup>& groups) {
    std::set<NomadString> textureNames;
    std::set<NomadString> spriteNames;
    NomadIndex textureCount = 0;
    NomadIndex spriteCount = 0;

    for (const auto& texture : m_textures.m_textures) {
        textureNames.insert(toLowerAscii(texture->getName()));
    }
    for (const auto& sprite : m_sprites.m_sprites) {
        spriteNames.insert(toLowerAscii(sprite->getName()));
    }

    for (const auto& group : groups) {
        if (group.name.empty()) {
            throw ResourceException("Packed sprite group name must not be empty");
        }

        textureCount += group.pages.size();
        spriteCount += group.sprites.size();

        for (NomadIndex pageIndex = 0; pageIndex < group.pages.size(); ++pageIndex) {
            const auto& page = group.pages[pageIndex];
            const auto textureName = makePackedTextureName(group.name, pageIndex);

            if (!textureNames.insert(toLowerAscii(textureName)).second) {
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
            if (!spriteNames.insert(toLowerAscii(sprite.name)).second) {
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
            const auto textureName = makePackedTextureName(group.name, pageIndex);
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

    m_textures.m_textures.reserve(m_textures.m_textures.size() + textures.size());
    m_sprites.m_sprites.reserve(m_sprites.m_sprites.size() + sprites.size());

    for (auto& texture : textures) {
        m_textures.m_textures.push_back(std::move(texture));
    }
    for (auto& sprite : sprites) {
        m_sprites.m_sprites.push_back(std::move(sprite));
    }
}

NomadString ResourceManager::makeResourceName(const NomadString& basePath, const NomadString& fileName) {
    return basePath + "/" + fileName;
}

} // nomad
