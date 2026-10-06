// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/Texture.hpp>

#include <nomad/game/Canvas.hpp>
#include <nomad/game/Game.hpp>

#include <nomad/resource/ResourceManager.hpp>

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <cassert>

namespace nomad {

Texture::Texture(const NomadString& name, const NomadString& fileName, Game* game):
    Resource(name)
{
    auto surface = IMG_Load(fileName.c_str());

    SDL_Renderer* renderer = game->getCanvas()->getSdlRenderer();

    m_texture = SDL_CreateTextureFromSurface(renderer, surface);

    initTextureSize();

    SDL_DestroySurface(surface);

    if (m_texture == nullptr) {
        throw ResourceException("Failed to load image: " + fileName + "\n" + SDL_GetError());
    }
}

Texture::Texture(const NomadString &name, Game* game, NomadInteger width, NomadInteger height):
    Resource(name),
    m_width(width),
    m_height(height)
{
    auto renderer = game->getCanvas()->getSdlRenderer();

    m_texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        static_cast<int>(width),
        static_cast<int>(height)
    );

    if (m_texture == nullptr) {
        throw ResourceException("Failed to create texture. Reason: " + NomadString(SDL_GetError()));
    }
}

Texture::Texture(const NomadString &name, SDL_Texture *texture):
    Resource(name),
    m_texture(texture)
{
    initTextureSize();
}

Texture::~Texture() {
    if (m_texture != nullptr) {
        SDL_DestroyTexture(m_texture);

        m_texture = nullptr;
    }
}

NomadInteger Texture::getWidth() const {
    return m_width;
}

NomadInteger Texture::getHeight() const {
    return m_height;
}

NomadBoolean Texture::lock() {
    if (m_locked) {
        return false;
    }

    auto result = SDL_LockTexture(m_texture, nullptr, &m_pixels, &m_pitch);

    if (result != 0) {
        log::error("Failed to lock texture. Reason: " + NomadString(SDL_GetError()));
        return false;
    }

    m_locked = true;

    m_pixelFormat = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA8888);

    return true;
}

void Texture::unlock() {
    if (!m_locked) {
        log::warning("Calling 'unlock' on an unlocked texture");
    }

    SDL_UnlockTexture(m_texture);

    m_locked = false;
    m_pixels = nullptr;
    m_pitch = 0;
}

void Texture::setPixel(
    const NomadInteger x,
    const NomadInteger y,
    const Color color
) const {
    if (!m_locked) {
        log::warning("Attempting to set pixel on an unlocked texture");
        return;
    }

    // We assume the format is SDL_PIXELFORMAT_RGBA8888
    auto pixels = static_cast<Uint32*>(m_pixels);

    Uint32 pixelColor = SDL_MapRGBA(
        m_pixelFormat,
        NULL,
        color.getRed(),
        color.getGreen(),
        color.getBlue(),
        color.getAlpha()
    );

    if (x >= 0 && x < m_width && y >= 0 && y < m_height) {
        pixels[y * (m_pitch / 4) + x] = pixelColor;
    }
}

SDL_Texture* Texture::getSdlTexture() const {
    assert(m_texture != nullptr);

    return m_texture;
}

void Texture::initTextureSize() {
    float width, height;

    SDL_GetTextureSize(m_texture, &width, &height);

    m_width = static_cast<NomadInteger>(width);
    m_height = static_cast<NomadInteger>(height);
}

///////////////////////////////////////////////////////////////////////////////
// TextureManager
TextureManager::TextureManager(ResourceManager* resources):
    m_resources(resources)
{
}

NomadId TextureManager::registerTexture(const NomadString& textureName) {
    log::debug("Loading texture: " + textureName);

    const NomadString file_name = m_resources->makeResourcePath(textureName);

    auto texture_id = toNomadId(m_textures.size());

    m_textures.emplace_back(
        std::make_unique<Texture>(
            textureName,
            file_name,
            m_resources->getGame()
        )
    );

    return texture_id;
}

const Texture* TextureManager::getTexture(NomadId textureId) const {
    const auto textureIndex = toNomadIndex(textureId);

    if (textureIndex >= m_textures.size()) {
        return nullptr;
    }

    return m_textures[textureIndex].get();
}

const Texture* TextureManager::getTextureByName(const NomadString& textureName) const {
    for (const auto& texture : m_textures) {
        if (texture->getName() == textureName) {
            return texture.get();
        }
    }

    return nullptr;
}

Texture * TextureManager::createTexture(const NomadString &name, NomadInteger width, NomadInteger height) const {
    auto texture = new Texture(name, m_resources->getGame(), width, height);

    return texture;
}
} // nomad
