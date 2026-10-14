// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/AudioDiscovery.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/String.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <system_error>

namespace nomad {

namespace {

constexpr std::array<NomadStringView, 4> NOMAD_AUDIO_FILE_EXTENSIONS = {
    ".flac",
    ".mp3",
    ".ogg",
    ".wav",
};

[[noreturn]] void raiseFilesystemError(
    const NomadStringView operation,
    const NomadPath& path,
    const std::error_code& error
) {
    auto message = NomadString(operation) + " '" + pathToUtf8(path) + "'";

    if (error) {
        message += ": " + error.message();
    }

    throw AudioDiscoveryError(message);
}

bool isAudioFile(const NomadPath& path) {
    const auto extension = stringToLowerAscii(pathToUtf8(path.extension()));
    return std::ranges::find(NOMAD_AUDIO_FILE_EXTENSIONS, extension) != NOMAD_AUDIO_FILE_EXTENSIONS.end();
}

NomadString makeDottedName(const NomadPath& path) {
    auto name = pathToGenericUtf8(path);
    std::ranges::replace(name, '/', '.');

    if (name.empty()) {
        throw AudioDiscoveryError("Audio path must produce a non-empty resource name");
    }

    return name;
}

void addDirectorySources(
    const NomadPath& resourcePath,
    const NomadPath& configuredDirectory,
    const bool directoriesExplicitlyConfigured,
    std::vector<AudioSourceDefinition>& sources
) {
    const auto directory = (resourcePath / configuredDirectory).lexically_normal();
    const auto relativeDirectory = directory.lexically_relative(resourcePath);

    if (
        relativeDirectory.empty() ||
        relativeDirectory.is_absolute() ||
        *relativeDirectory.begin() == ".."
    ) {
        throw AudioDiscoveryError(
            "Audio directory must remain within the resource directory: '" +
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

            throw AudioDiscoveryError("Audio directory does not exist: '" + pathToUtf8(directory) + "'");
        }

        raiseFilesystemError("Failed to inspect audio directory", directory, error);
    }

    if (!std::filesystem::exists(status)) {
        if (!directoriesExplicitlyConfigured) {
            return;
        }

        throw AudioDiscoveryError("Audio directory does not exist: '" + pathToUtf8(directory) + "'");
    }

    if (std::filesystem::is_symlink(status)) {
        throw AudioDiscoveryError("Audio directory must not be a symbolic link: '" + pathToUtf8(directory) + "'");
    }

    if (!std::filesystem::is_directory(status)) {
        throw AudioDiscoveryError("Audio directory does not exist: '" + pathToUtf8(directory) + "'");
    }

    std::filesystem::recursive_directory_iterator iterator(directory, error);
    const std::filesystem::recursive_directory_iterator end;

    while (iterator != end) {
        if (error) {
            raiseFilesystemError("Failed to read audio directory", directory, error);
        }

        const auto& entry = *iterator;
        error.clear();
        const auto entryStatus = entry.symlink_status(error);

        if (error) {
            raiseFilesystemError("Failed to inspect audio resource", entry.path(), error);
        }

        if (std::filesystem::is_symlink(entryStatus)) {
            throw AudioDiscoveryError(
                "Audio directories must not contain symbolic links: '" + pathToUtf8(entry.path()) + "'"
            );
        }

        if (std::filesystem::is_regular_file(entryStatus) && isAudioFile(entry.path())) {
            const auto relativeFile = entry.path().lexically_relative(directory);
            auto resourceRelativeFile = configuredDirectory / relativeFile;
            resourceRelativeFile.replace_extension();

            if (!resourceRelativeFile.has_filename()) {
                throw AudioDiscoveryError(
                    "Audio file must produce a non-empty resource name: '" + pathToUtf8(entry.path()) + "'"
                );
            }

            sources.emplace_back(entry.path(), makeDottedName(resourceRelativeFile));
        }

        iterator.increment(error);
    }

    if (error) {
        raiseFilesystemError("Failed to read audio directory", directory, error);
    }
}

} // namespace

std::vector<AudioSourceDefinition> discoverAudioSources(
    const NomadPath& resourcePath,
    const std::vector<NomadPath>& directories,
    const bool directoriesExplicitlyConfigured
) {
    std::vector<AudioSourceDefinition> sources;

    for (const auto& directory : directories) {
        addDirectorySources(resourcePath, directory, directoriesExplicitlyConfigured, sources);
    }

    std::ranges::sort(sources, [](const auto& left, const auto& right) {
        const auto leftName = stringToLowerAscii(left.name);
        const auto rightName = stringToLowerAscii(right.name);

        if (leftName == rightName) {
            return left.name < right.name;
        }

        return leftName < rightName;
    });

    for (NomadIndex index = 1; index < sources.size(); ++index) {
        if (stringToLowerAscii(sources[index - 1].name) == stringToLowerAscii(sources[index].name)) {
            throw AudioDiscoveryError(
                "Audio resource name collision for '" + sources[index].name + "': '" +
                pathToUtf8(sources[index - 1].filePath) + "' and '" +
                pathToUtf8(sources[index].filePath) + "'"
            );
        }
    }

    return sources;
}

} // namespace nomad
