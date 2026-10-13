// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/SpriteDiscovery.hpp>

#include <nomad/system/Path.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace nomad {

namespace {

[[noreturn]] void raiseFilesystemError(
    const NomadStringView operation,
    const NomadPath& path,
    const std::error_code& error
) {
    auto message = NomadString(operation) + " '" + pathToUtf8(path) + "'";

    if (error) {
        message += ": " + error.message();
    }

    throw SpriteDiscoveryError(message);
}

NomadString toLowerAscii(NomadString value) {
    for (auto& character : value) {
        character = static_cast<NomadChar>(std::tolower(static_cast<unsigned char>(character)));
    }

    return value;
}

bool isPngFile(const NomadPath& path) {
    return toLowerAscii(pathToUtf8(path.extension())) == ".png";
}

NomadString makeDottedName(const NomadPath& path) {
    auto name = pathToGenericUtf8(path);
    std::ranges::replace(name, '/', '.');

    if (name.empty()) {
        throw SpriteDiscoveryError("Sprite path must produce a non-empty resource name");
    }

    return name;
}

NomadPath firstPathComponent(const NomadPath& path) {
    const auto component = path.begin();

    if (component == path.end()) {
        throw SpriteDiscoveryError("Sprite path must contain a file name");
    }

    return *component;
}

void addDirectorySources(
    const NomadPath& resourcePath,
    const NomadPath& configuredDirectory,
    const bool directoriesExplicitlyConfigured,
    std::vector<SpriteSourceDefinition>& sources
) {
    const auto directory = (resourcePath / configuredDirectory).lexically_normal();
    const auto relativeDirectory = directory.lexically_relative(resourcePath);

    if (
        relativeDirectory.empty() ||
        relativeDirectory.is_absolute() ||
        *relativeDirectory.begin() == ".."
    ) {
        throw SpriteDiscoveryError(
            "Sprite directory must remain within the resource directory: '" +
            pathToUtf8(configuredDirectory) + "'"
        );
    }

    std::error_code error;
    const auto status = std::filesystem::symlink_status(directory, error);

    if (error) {
        if (error == std::errc::no_such_file_or_directory) {
            if (!directoriesExplicitlyConfigured) {
                return;
            }

            throw SpriteDiscoveryError("Sprite directory does not exist: '" + pathToUtf8(directory) + "'");
        }

        raiseFilesystemError("Failed to inspect sprite directory", directory, error);
    }

    if (!std::filesystem::exists(status)) {
        if (!directoriesExplicitlyConfigured) {
            return;
        }

        throw SpriteDiscoveryError("Sprite directory does not exist: '" + pathToUtf8(directory) + "'");
    }

    if (std::filesystem::is_symlink(status)) {
        throw SpriteDiscoveryError("Sprite directory must not be a symbolic link: '" + pathToUtf8(directory) + "'");
    }

    if (!std::filesystem::is_directory(status)) {
        throw SpriteDiscoveryError("Sprite directory does not exist: '" + pathToUtf8(directory) + "'");
    }

    std::filesystem::recursive_directory_iterator iterator(directory, error);
    const std::filesystem::recursive_directory_iterator end;

    while (iterator != end) {
        if (error) {
            raiseFilesystemError("Failed to read sprite directory", directory, error);
        }

        const auto& entry = *iterator;
        error.clear();
        const auto entryStatus = entry.symlink_status(error);

        if (error) {
            raiseFilesystemError("Failed to inspect sprite resource", entry.path(), error);
        }

        if (std::filesystem::is_symlink(entryStatus)) {
            throw SpriteDiscoveryError(
                "Sprite directories must not contain symbolic links: '" + pathToUtf8(entry.path()) + "'"
            );
        }

        if (std::filesystem::is_regular_file(entryStatus) && isPngFile(entry.path())) {
            const auto relativeFile = entry.path().lexically_relative(directory);
            auto resourceRelativeFile = configuredDirectory / relativeFile;
            resourceRelativeFile.replace_extension();

            if (!resourceRelativeFile.has_filename()) {
                throw SpriteDiscoveryError(
                    "Sprite file must produce a non-empty resource name: '" + pathToUtf8(entry.path()) + "'"
                );
            }

            const auto relativeParent = relativeFile.parent_path();
            const auto groupPath = relativeParent.empty()
                ? configuredDirectory
                : configuredDirectory / firstPathComponent(relativeParent);

            sources.emplace_back(
                entry.path(),
                makeDottedName(resourceRelativeFile),
                makeDottedName(groupPath)
            );
        }

        iterator.increment(error);
    }

    if (error) {
        raiseFilesystemError("Failed to read sprite directory", directory, error);
    }
}

} // namespace

std::vector<SpriteSourceDefinition> discoverSpriteSources(
    const NomadPath& resourcePath,
    const std::vector<NomadPath>& directories,
    const bool directoriesExplicitlyConfigured
) {
    std::vector<SpriteSourceDefinition> sources;

    for (const auto& directory : directories) {
        addDirectorySources(resourcePath, directory, directoriesExplicitlyConfigured, sources);
    }

    std::ranges::sort(sources, [](const auto& left, const auto& right) {
        const auto leftName = toLowerAscii(left.name);
        const auto rightName = toLowerAscii(right.name);

        if (leftName == rightName) {
            return left.name < right.name;
        }

        return leftName < rightName;
    });

    for (NomadIndex index = 1; index < sources.size(); ++index) {
        if (toLowerAscii(sources[index - 1].name) == toLowerAscii(sources[index].name)) {
            throw SpriteDiscoveryError(
                "Sprite resource name collision for '" + sources[index].name + "': '" +
                pathToUtf8(sources[index - 1].filePath) + "' and '" +
                pathToUtf8(sources[index].filePath) + "'"
            );
        }
    }

    return sources;
}

} // namespace nomad
