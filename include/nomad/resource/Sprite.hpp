// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/geometry/Point.hpp>
#include <nomad/geometry/Rectangle.hpp>

#include <nomad/resource/Resource.hpp>

namespace nomad {

class Texture;

///////////////////////////////////////////////////////////////////////////////
// Sprite
///////////////////////////////////////////////////////////////////////////////
class Sprite {
public:
    Sprite(const NomadString& spriteName, const Texture* texture, const Rectangle& source, const Rectangle& frame);

    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] const Rectangle& getSource() const;
    [[nodiscard]] const Rectangle& getFrame() const;
    [[nodiscard]] const Texture* getTexture() const;

    [[nodiscard]] NomadInteger getWidth() const;
    [[nodiscard]] NomadInteger getHeight() const;

private:
    NomadString m_name;
    const Texture* m_texture;
    Rectangle m_source;
    Rectangle m_frame;
};

///////////////////////////////////////////////////////////////////////////////
// SpriteManager
///////////////////////////////////////////////////////////////////////////////
class SpriteManager {
public:
    explicit SpriteManager(ResourceManager* resources);

    [[nodiscard]] NomadId registerSprite(
        const NomadString& sprite_name,
        const Texture* texture,
        const Rectangle& source,
        const Rectangle& frame
    );
    [[nodiscard]] const Sprite* getSprite(NomadId sprite_id) const;
    [[nodiscard]] const Sprite* getSpriteByName(const NomadString& name) const;
    void getSpritesByPrefix(const NomadString& prefix, std::vector<Sprite*>& sprites) const;

private:
    ResourceManager* m_resources;
    std::vector<std::unique_ptr<Sprite>> m_sprites;
};

} // namespace nomad
