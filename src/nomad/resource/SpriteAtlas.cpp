// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/SpriteAtlas.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/resource/ResourceManager.hpp>
#include <nomad/resource/Sprite.hpp>
#include <nomad/resource/Texture.hpp>

#include <boost/json.hpp>

#include <fstream>

namespace nomad {

void sprite_atlas_resource_factory(ResourceManager* resource_manager, const NomadString& resource_name) {
    try {
        log::debug("Loading sprite atlas: '" + resource_name + "'");

        NomadString path = resource_manager->makeResourcePath(resource_name);

        std::ifstream file(path);

        boost::json::stream_parser parser;

        char buffer[1024];

        while (!file.eof()) {
            file.read(buffer, sizeof(buffer));
            parser.write(buffer, file.gcount());
        }

        auto json = parser.release().as_object();

        // Texture name is the resource name but with the extension replaced by .png
        NomadString texture_name = resource_name.substr(0, resource_name.find_last_of('.')) + ".png";

        auto texture = resource_manager->getTextures()->getTextureByName(texture_name);

        auto sprites = json.at("sprites").as_array();

        for (auto sprite: sprites) {
            NomadString name = sprite.at("name").as_string().c_str();

            Rectangle source(
                sprite.at("source").at("x").as_int64(),
                sprite.at("source").at("y").as_int64(),
                sprite.at("source").at("width").as_int64(),
                sprite.at("source").at("height").as_int64()
            );

            Rectangle frame(
                sprite.at("frame").at("x").as_int64(),
                sprite.at("frame").at("y").as_int64(),
                sprite.at("frame").at("width").as_int64(),
                sprite.at("frame").at("height").as_int64()
            );

            log::debug("Loading sprite: '" + name + "'");

            auto sprite_id = resource_manager->getSprites()->registerSprite(
                name,
                texture,
                source,
                frame
            );

            if (sprite_id == NOMAD_INVALID_ID) {
                log::error("Internal error: Failed to register sprite: '" + name + "'");
            }
        }

        log::debug("Loading texture: '" + texture_name + "'");
    }
    catch (const boost::exception& /*e*/) {
        throw ResourceException("Failed to load sprite atlas: '" + resource_name + "'");
    }
    catch (const std::exception& e) {
        throw ResourceException("Failed to load sprite atlas: '" + resource_name + "' - " + e.what());
    }
}

} // nomad
