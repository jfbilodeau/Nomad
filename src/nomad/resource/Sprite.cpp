// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/Sprite.hpp>

#include <nomad/resource/Texture.hpp>
#include <nomad/resource/ResourceManager.hpp>

namespace nomad {

///////////////////////////////////////////////////////////////////////////////
// Sprite
Sprite::Sprite(const NomadString& spriteName, const Texture* texture, const Rectangle& source, const Rectangle& frame):
    m_name(spriteName),
    m_texture(texture),
    m_source(source),
    m_frame(frame)
{
}

const NomadString& Sprite::getName() const {
    return m_name;
}

const Rectangle& Sprite::getSource() const {
    return m_source;
}

const Rectangle& Sprite::getFrame() const {
    return m_frame;
}

const Texture* Sprite::getTexture() const {
    return m_texture;
}

NomadInteger Sprite::getWidth() const {
    return m_source.getWidth();
}

NomadInteger Sprite::getHeight() const {
    return m_source.getHeight();
}

///////////////////////////////////////////////////////////////////////////////
// SpriteManager
SpriteManager::SpriteManager(ResourceManager* resources):
    m_resources(resources)
{}


NomadId SpriteManager::registerSprite(
    const NomadString& sprite_name, const Texture* texture, const Rectangle& source, const Rectangle& frame
) {
    auto id = toNomadId(m_sprites.size());

    m_sprites.emplace_back(
        std::make_unique<Sprite>(
            sprite_name,
            texture,
            source,
            frame
        )
    );

    return id;
}

const Sprite* SpriteManager::getSprite(const NomadId sprite_id) const {
    if (sprite_id >= static_cast<NomadId>(m_sprites.size())) {
        return nullptr;
    }

    return m_sprites[sprite_id].get();
}

const Sprite* SpriteManager::getSpriteByName(const NomadString& name) const {
    for (const auto& sprite : m_sprites) {
        if (sprite->getName() == name) {
            return sprite.get();
        }
    }

    return nullptr;
}

void SpriteManager::getSpritesByPrefix(const NomadString& prefix, std::vector<Sprite*>& sprites) const {
    for (const auto& sprite : m_sprites) {
        if (sprite->getName().compare(0, prefix.size(), prefix) == 0) {
            sprites.push_back(sprite.get());
        }
    }
}

} // nomad
