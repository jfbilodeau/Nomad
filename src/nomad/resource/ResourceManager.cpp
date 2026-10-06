// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/ResourceManager.hpp>

#include <nomad/resource/Animation.hpp>
#include <nomad/resource/Font.hpp>
#include <nomad/resource/Sprite.hpp>
#include <nomad/resource/SpriteAtlas.hpp>
#include <nomad/resource/Texture.hpp>

#include <nomad/game/Game.hpp>

#include <boost/json.hpp>

#include <filesystem>
#include <fstream>

namespace nomad {

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

NomadString ResourceManager::makeResourceName(const NomadString& basePath, const NomadString& fileName) {
    return basePath + "/" + fileName;
}

} // nomad
