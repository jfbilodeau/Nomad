// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/geometry/Point.hpp>

#include <optional>
#include <vector>

namespace nomad {

struct SpriteOptions {
    std::optional<std::vector<NomadPath>> directories;
    std::optional<NomadInteger> maxTextureSize;
    std::optional<NomadInteger> padding;
    bool missingDirectoriesAllowed = false;
};

struct AudioOptions {
    std::optional<std::vector<NomadPath>> directories;
    bool missingDirectoriesAllowed = false;
};

struct TextOptions {
    std::optional<std::vector<NomadPath>> directories;
};

struct ScriptOptions {
    std::optional<std::vector<NomadPath>> directories;
};

struct GameOptions {
    NomadString resourcePath;
    NomadString entryFunction = "init";
    std::optional<NomadString> title;
    std::optional<Point> resolution;
    std::optional<Point> size;
    std::optional<NomadPath> splashImage;
    AudioOptions audio;
    SpriteOptions sprites;
    TextOptions text;
    ScriptOptions scripts;
    std::optional<bool> fullscreen;
    std::optional<bool> debug;
};

} // namespace nomad
