// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/resource/Resource.hpp>

#include <nomad/resource/Animation.hpp>
#include <nomad/resource/Font.hpp>
#include <nomad/resource/Sprite.hpp>
#include <nomad/resource/Text.hpp>
#include <nomad/resource/Texture.hpp>

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

class Game;

class ResourceException : public NomadException {
public:
    explicit ResourceException(const NomadString& message) : NomadException(message) {}
};

class ResourceManager {
public:
    explicit ResourceManager(Game* game, const NomadString& basePath);
    ResourceManager(const ResourceManager& other) = delete;
    ResourceManager(ResourceManager&& other) noexcept = delete;
    ResourceManager& operator=(const ResourceManager& other) = delete;
    ~ResourceManager();

    [[nodiscard]] Game* getGame() const;

    [[nodiscard]] const NomadString& getBasePath() const;
    [[nodiscard]] NomadString makeResourcePath(const NomadString& resourceName) const;

    // Individual resource managers
    AnimationManager* getAnimations();
    FontManager* getFonts();
    SpriteManager* getSprites();
    TextManager* getText();
    TextureManager* getTextures();

    // TODO: These specialized method should be move elsewhere...
    void loadSpriteAtlas(const NomadString& resourceName);

private:
    NomadString makeResourceName(const NomadString& basePath, const NomadString& fileName);

    Game* m_game;
    NomadString m_basePath;
    AnimationManager m_animations;
    FontManager m_fonts;
    SpriteManager m_sprites;
    TextManager m_text;
    TextureManager m_textures;
};

} // nomad
