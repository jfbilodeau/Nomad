// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/resource/Resource.hpp>

#include <nomad/resource/Animation.hpp>
#include <nomad/resource/Audio.hpp>
#include <nomad/resource/Font.hpp>
#include <nomad/resource/Sprite.hpp>
#include <nomad/resource/SpritePacker.hpp>
#include <nomad/resource/Text.hpp>
#include <nomad/resource/Texture.hpp>

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

class Game;

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
    AudioManager* getAudio();
    FontManager* getFonts();
    SpriteManager* getSprites();
    TextManager* getText();
    TextureManager* getTextures();

    [[nodiscard]] NomadIndex loadSpriteRoots(
        const std::vector<NomadPath>& directories,
        bool directoriesExplicitlyConfigured,
        NomadInteger maxTextureSize,
        NomadInteger padding,
        bool missingDirectoriesAllowed = false
    );
    void registerPackedSprites(const std::vector<PackedSpriteGroup>& groups);

private:
    NomadString makeResourceName(const NomadString& basePath, const NomadString& fileName);

    Game* m_game;
    NomadString m_basePath;
    AnimationManager m_animations;
    AudioManager m_audio;
    FontManager m_fonts;
    SpriteManager m_sprites;
    TextManager m_text;
    TextureManager m_textures;
};

} // nomad
