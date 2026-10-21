// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/GameConfiguration.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/String.hpp>

#include <algorithm>

namespace nomad {

namespace {

std::optional<std::vector<NomadPath>> getResourcePaths(
    const NomadConfiguration& configuration,
    const NomadStringView section
) {
    const auto field = NomadString(section) + ".directories";
    const auto directories = configuration.getStringArray(field);
    if (!directories) {
        return std::nullopt;
    }
    try {
        return normalizeRelativeDirectories(*directories);
    } catch (const NomadException& error) {
        throw NomadConfigurationException(
            "Configuration field '" + field + "' (relative to the game resource root) " + error.what()
        );
    }
}

NomadPath readRelativePath(
    const NomadString& value,
    const NomadStringView field,
    const NomadStringView base = "game resource root"
) {
    auto normalized = value;
    std::ranges::replace(normalized, '\\', '/');
    const auto path = pathFromUtf8(normalized).lexically_normal();
    if (path.empty() || path.has_root_path() || *path.begin() == "..") {
        throw NomadConfigurationException(
            "Configuration field '" + NomadString(field) + "' must be a path relative to the " + NomadString(base)
        );
    }
    return path;
}

std::optional<std::vector<NomadPath>> readTextFiles(const NomadConfiguration& configuration) {
    const auto files = configuration.getStringArray("text.directories");
    if (!files) {
        return std::nullopt;
    }
    std::vector<NomadPath> result;
    std::vector<NomadString> normalizedKeys;
    for (const auto& file : *files) {
        const auto path = readRelativePath(file, "text.directories");
        if (path.extension() != ".csv") {
            throw NomadConfigurationException(
                "Configuration field 'text.directories' must contain only CSV files"
            );
        }
        const auto key = stringToLowerAscii(pathToGenericUtf8(path));
        if (std::ranges::find(normalizedKeys, key) != normalizedKeys.end()) {
            throw NomadConfigurationException("Configuration field 'text.directories' contains duplicate files");
        }
        normalizedKeys.push_back(key);
        result.push_back(path);
    }
    return result;
}

std::optional<Point> readDimensions(
    const NomadConfiguration& configuration,
    const NomadStringView field
) {
    const auto dimensions = configuration.getIntegerArray(field);
    if (!dimensions) {
        return std::nullopt;
    }
    if (dimensions->size() != 2 || (*dimensions)[0] <= 0 || (*dimensions)[1] <= 0) {
        throw NomadConfigurationException(
            "Configuration field '" + NomadString(field) + "' must be an array of two positive integers"
        );
    }
    return Point{(*dimensions)[0], (*dimensions)[1]};
}

std::optional<NomadInteger> readSpriteInteger(
    const NomadConfiguration& configuration,
    const NomadStringView field,
    const NomadInteger minimum
) {
    const auto value = configuration.getInteger(field);
    if (value && *value < minimum) {
        throw NomadConfigurationException(
            "Configuration field '" + NomadString(field) + "' must be at least " + std::to_string(minimum)
        );
    }
    return value;
}

} // namespace

GameOptions readGameOptions(const NomadConfiguration& configuration) {
    for (const auto section : {"game", "window", "sprites", "text", "resources", "audio"}) {
        configuration.validateSection(section);
    }
    GameOptions options;
    options.resourcePath = pathToGenericUtf8(readRelativePath(
        configuration.getString("resources.directory").value_or("res"), "resources.directory", "project root"
    ));
    options.entryFunction = configuration.getString("game.entry").value_or(options.entryFunction);
    if (options.entryFunction.empty()) {
        throw NomadConfigurationException("Configuration field 'game.entry' must be a non-empty string");
    }
    options.debug = configuration.getBoolean("game.debug");
    options.title = configuration.getString("window.title");
    if (options.title && options.title->empty()) {
        throw NomadConfigurationException("Configuration field 'window.title' must be a non-empty string");
    }
    options.resolution = readDimensions(configuration, "window.resolution");
    options.size = readDimensions(configuration, "window.size");
    options.fullscreen = configuration.getBoolean("window.fullscreen");
    if (const auto value = configuration.getString("window.splash-image")) {
        const auto path = readRelativePath(*value, "window.splash-image");
        if (path == "." || !path.has_filename()) {
            throw NomadConfigurationException(
                "Configuration field 'window.splash-image' must be a resource-relative file"
            );
        }
        options.splashImage = path;
    }
    options.audio.directories = getResourcePaths(configuration, "audio");
    options.sprites.directories = getResourcePaths(configuration, "sprites");
    options.sprites.maxTextureSize = readSpriteInteger(configuration, "sprites.max-texture-size", 1);
    options.sprites.padding = readSpriteInteger(configuration, "sprites.padding", 0);
    options.text.directories = readTextFiles(configuration);
    return options;
}

NomadPath resolveGameResourcePath(
    const NomadConfiguration& configuration,
    const NomadPath& projectDirectory
) {
    return (projectDirectory / pathFromUtf8(readGameOptions(configuration).resourcePath)).lexically_normal();
}

void normalizeGameConfiguration(const NomadConfiguration& source, NomadConfiguration& release) {
    const auto options = readGameOptions(source);
    release.setString("resources.directory", "res");
    if (options.splashImage) {
        release.setString("window.splash-image", pathToGenericUtf8(*options.splashImage));
    }
    const auto writeDirectories = [&release](
        const NomadStringView field,
        const std::optional<std::vector<NomadPath>>& directories
    ) {
        if (directories) {
            std::vector<NomadString> paths;
            for (const auto& path : *directories) {
                paths.push_back(pathToGenericUtf8(path));
            }
            release.setStringArray(field, paths);
        }
    };
    writeDirectories("audio.directories", options.audio.directories);
    writeDirectories("sprites.directories", options.sprites.directories);
    writeDirectories("text.directories", options.text.directories);
}

} // namespace nomad
