// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/resource/Resource.hpp>

#include <nomad/game/Color.hpp>

#include <vector>

// Static forward declaration
struct SDL_Texture;

namespace nomad {

// Nomad forward declaration
class Game;
class ResourceManager;

///////////////////////////////////////////////////////////////////////////////
// Texture
///////////////////////////////////////////////////////////////////////////////
class Texture : public Resource {
public:
    Texture(const NomadString& name, const NomadString& fileName, Game* game);
    Texture(const NomadString& name, Game* game, NomadInteger width, NomadInteger height);
    Texture(const NomadString& name, SDL_Texture* texture);
    Texture(const Texture& other) = delete;
    ~Texture() override;

    [[nodiscard]] NomadInteger getWidth() const;
    [[nodiscard]] NomadInteger getHeight() const;

    // Lock the texture to access its pixels
    [[nodiscard]] NomadBoolean lock();
    void unlock();

    // Get the texture pixels
    void setPixel(NomadInteger x, NomadInteger y, Color color) const;

    [[nodiscard]] SDL_Texture* getSdlTexture() const;

private:
    void initTextureSize();

    SDL_Texture* m_texture = nullptr;
    NomadBoolean m_locked = false;
    void* m_pixels = nullptr;
    int m_pitch = 0;
    const SDL_PixelFormatDetails* m_pixelFormat = nullptr;
    NomadInteger m_width = 0;
    NomadInteger m_height = 0;
};

///////////////////////////////////////////////////////////////////////////////
// TextureManager
///////////////////////////////////////////////////////////////////////////////
class TextureManager {
public:
    explicit TextureManager(ResourceManager* resources);

    [[nodiscard]] NomadId registerTexture(const NomadString& textureName);
    [[nodiscard]] const Texture* getTexture(NomadId textureId) const;
    [[nodiscard]] const Texture* getTextureByName(const NomadString& textureName) const;
    [[nodiscard]] Texture* createTexture(const NomadString& name, NomadInteger width, NomadInteger height) const;

private:
    ResourceManager* m_resources;
    std::vector<std::unique_ptr<Texture>> m_textures;
};

} // nomad
