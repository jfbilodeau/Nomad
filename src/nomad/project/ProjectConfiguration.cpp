// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/system/ModuleManifest.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/system/String.hpp>

#include <toml++/toml.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace nomad {

namespace {

NomadString requiredString(
    const toml::table& table,
    const NomadStringView tableName,
    const NomadStringView key
) {
    const auto value = table[key].value<NomadString>();

    if (!value || value->empty()) {
        throw ProjectConfigurationException(
            "Configuration field '" + NomadString(tableName) + "." + NomadString(key) +
            "' must be a non-empty string"
        );
    }

    return *value;
}

const toml::table& requiredTable(const toml::table& document, const NomadStringView name) {
    const auto* table = document[name].as_table();

    if (table == nullptr) {
        throw ProjectConfigurationException("Configuration table '[" + NomadString(name) + "]' is required");
    }

    return *table;
}

NomadString optionalString(
    const toml::table& table,
    const NomadStringView tableName,
    const NomadStringView key,
    const NomadStringView fallback
) {
    return table.contains(key) ? requiredString(table, tableName, key) : NomadString(fallback);
}

const toml::table* optionalTable(const toml::table& document, const NomadStringView name) {
    if (!document.contains(name)) {
        return nullptr;
    }
    return &requiredTable(document, name);
}

NomadString escapeTomlString(const NomadStringView value) {
    NomadString escaped;
    escaped.reserve(value.size());

    for (const auto character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += character; break;
        }
    }

    return escaped;
}

void validateExecutable(const NomadString& executable) {
    const auto path = pathFromUtf8(executable);
    const auto containsSeparator =
        executable.find('/') != NomadString::npos || executable.find('\\') != NomadString::npos;

    if (containsSeparator || path.has_extension() || executable == "." || executable == "..") {
        throw ProjectConfigurationException(
            "Configuration field 'project.executable' must be an extensionless file name"
        );
    }
}

void validateProjectRelativePath(
    const NomadPath& path,
    const NomadStringView field,
    const bool allowProjectRoot
) {
    const auto normalized = path.lexically_normal();

    if (
        path.is_absolute() ||
        normalized.empty() ||
        (!allowProjectRoot && normalized == ".") ||
        *normalized.begin() == ".."
    ) {
        throw ProjectConfigurationException(
            "Configuration field '" + NomadString(field) + "' must be a project-relative directory"
        );
    }
}

std::vector<NomadString> readExclusions(const toml::table& package) {
    const auto* exclusions = package["exclude"].as_array();

    if (exclusions == nullptr) {
        throw ProjectConfigurationException("Configuration field 'package.exclude' must be an array of strings");
    }

    std::vector<NomadString> result;
    result.reserve(exclusions->size());

    for (const auto& exclusion : *exclusions) {
        const auto value = exclusion.value<NomadString>();

        if (!value || value->empty()) {
            throw ProjectConfigurationException(
                "Configuration field 'package.exclude' must contain only non-empty strings"
            );
        }

        result.push_back(*value);
    }

    return result;
}

NomadString normalizedDirectoryKey(const NomadPath& path) {
    return stringToLowerAscii(pathToGenericUtf8(path.lexically_normal()));
}

bool directoriesOverlap(const NomadStringView left, const NomadStringView right) {
    return
        left == right ||
        (left.size() < right.size() && right.starts_with(left) && right[left.size()] == '/') ||
        (right.size() < left.size() && left.starts_with(right) && left[right.size()] == '/');
}

std::vector<NomadPath> readAssetDirectories(
    const toml::table& table,
    const NomadStringView tableName
) {
    const auto* directories = table["directories"].as_array();
    const auto field = NomadString(tableName) + ".directories";

    if (directories == nullptr) {
        throw ProjectConfigurationException("Configuration field '" + field + "' must be an array of strings");
    }

    std::vector<NomadPath> result;
    std::vector<NomadString> normalizedKeys;
    result.reserve(directories->size());
    normalizedKeys.reserve(directories->size());

    for (const auto& directory : *directories) {
        const auto value = directory.value<NomadString>();

        if (!value || value->empty()) {
            throw ProjectConfigurationException(
                "Configuration field '" + field + "' must contain only non-empty strings"
            );
        }

        auto normalizedValue = *value;
        std::ranges::replace(normalizedValue, '\\', '/');
        auto path = pathFromUtf8(normalizedValue).lexically_normal();

        if (!path.empty() && !path.has_filename()) {
            path = path.parent_path();
        }

        validateProjectRelativePath(path, field, false);
        const auto key = normalizedDirectoryKey(path);

        for (const auto& existing : normalizedKeys) {
            if (directoriesOverlap(existing, key)) {
                throw ProjectConfigurationException(
                    "Configuration field '" + field + "' contains duplicate or overlapping directories"
                );
            }
        }

        normalizedKeys.push_back(key);
        result.push_back(std::move(path));
    }

    return result;
}

std::vector<NomadPath> readTextDirectories(const toml::table& text) {
    const auto* directories = text["directories"].as_array();
    constexpr auto field = "text.directories";

    if (directories == nullptr) {
        throw ProjectConfigurationException("Configuration field 'text.directories' must be an array of strings");
    }

    std::vector<NomadPath> result;
    std::vector<NomadString> normalizedKeys;
    result.reserve(directories->size());
    normalizedKeys.reserve(directories->size());

    for (const auto& directory : *directories) {
        const auto value = directory.value<NomadString>();

        if (!value || value->empty()) {
            throw ProjectConfigurationException(
                "Configuration field 'text.directories' must contain only non-empty strings"
            );
        }

        auto normalizedValue = *value;
        std::ranges::replace(normalizedValue, '\\', '/');
        const auto path = pathFromUtf8(normalizedValue).lexically_normal();
        validateProjectRelativePath(path, field, false);

        if (path.extension() != ".csv") {
            throw ProjectConfigurationException(
                "Configuration field 'text.directories' must contain only CSV files"
            );
        }

        const auto key = normalizedDirectoryKey(path);

        if (std::ranges::find(normalizedKeys, key) != normalizedKeys.end()) {
            throw ProjectConfigurationException(
                "Configuration field 'text.directories' contains duplicate files"
            );
        }

        normalizedKeys.push_back(key);
        result.push_back(path);
    }

    return result;
}

NomadPath readResourceRelativeFile(
    const toml::table& table,
    const NomadStringView tableName,
    const NomadStringView key
) {
    auto value = requiredString(table, tableName, key);
    std::ranges::replace(value, '\\', '/');
    const auto path = pathFromUtf8(value).lexically_normal();
    const auto field = NomadString(tableName) + "." + NomadString(key);

    if (
        path.is_absolute() ||
        path.empty() ||
        path == "." ||
        *path.begin() == ".." ||
        !path.has_filename()
    ) {
        throw ProjectConfigurationException(
            "Configuration field '" + field + "' must be a resource-relative file"
        );
    }

    return path;
}

std::pair<NomadInteger, NomadInteger> readPositiveDimensions(
    const toml::table& window,
    const NomadStringView key
) {
    const auto* dimensions = window[key].as_array();
    const auto field = "window." + NomadString(key);

    if (dimensions == nullptr || dimensions->size() != 2) {
        throw ProjectConfigurationException(
            "Configuration field '" + field + "' must be an array of two positive integers"
        );
    }

    const auto width = (*dimensions)[0].value<NomadInteger>();
    const auto height = (*dimensions)[1].value<NomadInteger>();

    if (
        !(*dimensions)[0].is_integer() ||
        !(*dimensions)[1].is_integer() ||
        !width ||
        !height ||
        *width <= 0 ||
        *height <= 0
    ) {
        throw ProjectConfigurationException(
            "Configuration field '" + field + "' must be an array of two positive integers"
        );
    }

    return {*width, *height};
}

bool readBoolean(
    const toml::table& table,
    const NomadStringView tableName,
    const NomadStringView key
) {
    const auto& node = table[key];
    const auto value = node.value<bool>();

    if (!node.is_boolean() || !value) {
        throw ProjectConfigurationException(
            "Configuration field '" + NomadString(tableName) + "." + NomadString(key) +
            "' must be a boolean"
        );
    }

    return *value;
}

NomadInteger readSpriteInteger(
    const toml::table& sprites,
    const NomadStringView key,
    const NomadInteger minimum
) {
    const auto& node = sprites[key];
    const auto value = sprites[key].value<NomadInteger>();
    const auto field = "sprites." + NomadString(key);

    if (!node.is_integer() || !value) {
        throw ProjectConfigurationException("Configuration field '" + field + "' must be an integer");
    }

    if (*value < minimum) {
        throw ProjectConfigurationException(
            "Configuration field '" + field + "' must be at least " + std::to_string(minimum)
        );
    }

    return *value;
}

NomadString serializeSpriteConfiguration(const SpriteOptions& options) {
    if (!options.directories && !options.maxTextureSize && !options.padding) {
        return {};
    }

    NomadString result = "\n[sprites]\n";

    if (options.directories) {
        result += "directories = [";

        for (NomadIndex index = 0; index < options.directories->size(); ++index) {
            if (index != 0) {
                result += ", ";
            }

            result += "\"" + escapeTomlString(pathToGenericUtf8((*options.directories)[index])) + "\"";
        }

        result += "]\n";
    }

    if (options.maxTextureSize) {
        result += "max-texture-size = " + std::to_string(*options.maxTextureSize) + "\n";
    }

    if (options.padding) {
        result += "padding = " + std::to_string(*options.padding) + "\n";
    }

    return result;
}

NomadString serializeAudioConfiguration(const AudioOptions& options) {
    if (!options.directories) {
        return {};
    }

    NomadString result = "\n[audio]\ndirectories = [";

    for (NomadIndex index = 0; index < options.directories->size(); ++index) {
        if (index != 0) {
            result += ", ";
        }

        result += "\"" + escapeTomlString(pathToGenericUtf8((*options.directories)[index])) + "\"";
    }

    result += "]\n";
    return result;
}

NomadString serializeTextConfiguration(const TextOptions& options) {
    if (!options.directories) {
        return {};
    }

    NomadString result = "\n[text]\ndirectories = [";

    for (NomadIndex index = 0; index < options.directories->size(); ++index) {
        if (index != 0) {
            result += ", ";
        }

        result += "\"" + escapeTomlString(pathToGenericUtf8((*options.directories)[index])) + "\"";
    }

    result += "]\n";
    return result;
}

NomadString serializeScriptConfiguration(const ScriptOptions& options) {
    if (!options.directories) {
        return {};
    }

    NomadString result = "\n[scripts]\ndirectories = [";

    for (NomadIndex index = 0; index < options.directories->size(); ++index) {
        if (index != 0) {
            result += ", ";
        }

        result += "\"" + escapeTomlString(pathToGenericUtf8((*options.directories)[index])) + "\"";
    }

    result += "]\n";
    return result;
}

NomadString serializeWindowConfiguration(const GameOptions& options) {
    if (
        !options.title &&
        !options.resolution &&
        !options.size &&
        !options.fullscreen &&
        !options.splashImage
    ) {
        return {};
    }

    NomadString result = "\n[window]\n";

    if (options.title) {
        result += "title = \"" + escapeTomlString(*options.title) + "\"\n";
    }
    if (options.resolution) {
        result +=
            "resolution = [" + std::to_string(options.resolution->getX()) + ", " +
            std::to_string(options.resolution->getY()) + "]\n";
    }
    if (options.size) {
        result +=
            "size = [" + std::to_string(options.size->getX()) + ", " +
            std::to_string(options.size->getY()) + "]\n";
    }
    if (options.fullscreen) {
        result += "fullscreen = " + NomadString(*options.fullscreen ? "true" : "false") + "\n";
    }
    if (options.splashImage) {
        result +=
            "splash-image = \"" +
            escapeTomlString(pathToGenericUtf8(*options.splashImage)) + "\"\n";
    }

    return result;
}

NomadString serializeGameConfiguration(const GameOptions& options) {
    if (!options.debug) {
        return {};
    }

    return "\n[game]\ndebug = " + NomadString(*options.debug ? "true" : "false") + "\n";
}

NomadString formatParseError(const toml::parse_error& error) {
    std::ostringstream message;
    message
        << "Failed to parse project configuration at "
        << error.source().begin.line
        << ':'
        << error.source().begin.column
        << ": "
        << error.description();
    return message.str();
}

} // namespace

std::optional<NomadPath> findProjectRoot(const NomadPath& startPath) {
    std::error_code error;
    auto current = std::filesystem::absolute(startPath, error);

    if (error) {
        throw ProjectConfigurationException(
            "Failed to resolve project search path '" + pathToUtf8(startPath) + "': " + error.message()
        );
    }

    if (std::filesystem::is_regular_file(current, error)) {
        current = current.parent_path();
    } else if (error) {
        throw ProjectConfigurationException(
            "Failed to inspect project search path '" + pathToUtf8(current) + "': " + error.message()
        );
    }

    while (true) {
        error.clear();

        if (std::filesystem::is_regular_file(current / NOMAD_PROJECT_FILE_NAME, error)) {
            return current;
        }

        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
        }

        if (error) {
            throw ProjectConfigurationException(
                "Failed to inspect project directory '" + pathToUtf8(current) + "': " + error.message()
            );
        }

        const auto parent = current.parent_path();

        if (parent == current || parent.empty()) {
            return std::nullopt;
        }

        current = parent;
    }
}

GameOptions readGameOptions(const NomadConfiguration& configuration) {
    const auto document = toml::parse(configuration.serialize());
    const auto* project = optionalTable(document, "project");
    const auto* game = optionalTable(document, "game");
    const auto* window = optionalTable(document, "window");
    const auto* audio = optionalTable(document, "audio");
    const auto* sprites = optionalTable(document, "sprites");
    const auto* text = optionalTable(document, "text");
    const auto* scripts = optionalTable(document, "scripts");
    GameOptions options;

    if (project != nullptr) {
        options.entryFunction = optionalString(*project, "project", "entry", options.entryFunction);
    }
    if (game != nullptr && game->contains("debug")) {
        options.debug = readBoolean(*game, "game", "debug");
    }
    if (window != nullptr && window->contains("title")) {
        options.title = requiredString(*window, "window", "title");
    }
    if (window != nullptr && window->contains("resolution")) {
        const auto [width, height] = readPositiveDimensions(*window, "resolution");
        options.resolution = Point{width, height};
    }
    if (window != nullptr && window->contains("size")) {
        const auto [width, height] = readPositiveDimensions(*window, "size");
        options.size = Point{width, height};
    }
    if (window != nullptr && window->contains("fullscreen")) {
        options.fullscreen = readBoolean(*window, "window", "fullscreen");
    }
    if (window != nullptr && window->contains("splash-image")) {
        options.splashImage = readResourceRelativeFile(*window, "window", "splash-image");
    }
    if (audio != nullptr && audio->contains("directories")) {
        options.audio.directories = readAssetDirectories(*audio, "audio");
    }
    if (sprites != nullptr && sprites->contains("directories")) {
        options.sprites.directories = readAssetDirectories(*sprites, "sprites");
    }
    if (sprites != nullptr && sprites->contains("max-texture-size")) {
        options.sprites.maxTextureSize = readSpriteInteger(*sprites, "max-texture-size", 1);
    }
    if (sprites != nullptr && sprites->contains("padding")) {
        options.sprites.padding = readSpriteInteger(*sprites, "padding", 0);
    }
    if (text != nullptr && text->contains("directories")) {
        options.text.directories = readTextDirectories(*text);
    }
    if (scripts != nullptr && scripts->contains("directories")) {
        options.scripts.directories = readAssetDirectories(*scripts, "scripts");
    }

    return options;
}

NomadProject loadNomadProject(const NomadPath& projectFile) {
    toml::table document;

    std::ifstream input(projectFile, std::ios::binary);

    if (!input) {
        throw ProjectConfigurationException(
            "Failed to open project configuration '" + pathToUtf8(projectFile) + "' for reading"
        );
    }

    try {
        document = toml::parse(input, pathToUtf8(projectFile));
    } catch (const toml::parse_error& error) {
        throw ProjectConfigurationException(formatParseError(error));
    }

    const auto schema = document["schema"].value<NomadInteger>();

    if (!document["schema"].is_integer() || !schema) {
        throw ProjectConfigurationException("Configuration field 'schema' must be an integer");
    }

    if (*schema != NOMAD_PROJECT_SCHEMA_VERSION) {
        throw ProjectConfigurationException(
            "Unsupported project schema " + std::to_string(*schema) +
            "; expected " + std::to_string(NOMAD_PROJECT_SCHEMA_VERSION)
        );
    }

    const auto& project = requiredTable(document, "project");
    const auto& nomad = requiredTable(document, "nomad");
    const auto* resources = optionalTable(document, "resources");
    const auto* package = optionalTable(document, "package");
    const auto hasExecutable = project.contains("executable");

    if (!hasExecutable && (resources != nullptr || package != nullptr)) {
        throw ProjectConfigurationException(
            "Development configuration requires 'project.executable' when '[resources]' or '[package]' is supplied"
        );
    }

    NomadProject result;
    std::ostringstream retainedDocument;
    retainedDocument << toml::toml_formatter{document};
    result.document = NomadConfiguration::parse(retainedDocument.str());
    auto& configuration = result.configuration;
    auto& gameOptions = result.gameOptions;
    configuration.schema = *schema;
    configuration.type = hasExecutable
        ? ProjectConfigurationType::Development
        : ProjectConfigurationType::Release;
    configuration.project.name = requiredString(project, "project", "name");
    configuration.project.identifier = requiredString(project, "project", "identifier");
    configuration.project.version = requiredString(project, "project", "version");
    gameOptions = readGameOptions(result.document);
    const auto nomadVersion = requiredString(nomad, "nomad", "version");

    try {
        configuration.sdk.version = NomadVersion::parse(nomadVersion);
    } catch (const NomadVersionException& error) {
        throw ProjectConfigurationException(
            "Configuration field 'nomad.version' is invalid: " + NomadString(error.what())
        );
    }

    {
        try {
            configuration.sdk.modules = result.document.requireStringArray("nomad.modules");
        } catch (const NomadConfigurationException& error) {
            throw ProjectConfigurationException(error.what());
        }

        std::vector<NomadString> names;
        for (const auto& name : configuration.sdk.modules) {
            if (!isValidModuleName(name)) {
                throw ProjectConfigurationException(
                    "Configuration field 'nomad.modules' must contain only logical module names"
                );
            }
            if (std::ranges::find(names, name) != names.end()) {
                throw ProjectConfigurationException(
                    "Configuration field 'nomad.modules' contains duplicate module '" + name + "'"
                );
            }
            names.push_back(name);
        }
    }

    if (configuration.type == ProjectConfigurationType::Development) {
        configuration.project.executable = requiredString(project, "project", "executable");
        if (resources != nullptr) {
            configuration.resources.directory = pathFromUtf8(optionalString(
                *resources, "resources", "directory", pathToUtf8(configuration.resources.directory)));
        }
        if (package != nullptr) {
            configuration.package.output = pathFromUtf8(optionalString(
                *package, "package", "output", pathToUtf8(configuration.package.output)));
            if (package->contains("exclude")) {
                configuration.package.exclude = readExclusions(*package);
            }
        }
        validateExecutable(configuration.project.executable);
        validateProjectRelativePath(configuration.resources.directory, "resources.directory", true);
        validateProjectRelativePath(configuration.package.output, "package.output", false);
    } else {
        configuration.package.output.clear();
        configuration.package.exclude.clear();
    }

    if (gameOptions.entryFunction.empty()) {
        throw ProjectConfigurationException("Configuration field 'project.entry' must be a non-empty string");
    }

    std::error_code error;
    result.root = std::filesystem::absolute(projectFile.parent_path(), error);

    if (error) {
        throw ProjectConfigurationException(
            "Failed to resolve project root '" + pathToUtf8(projectFile.parent_path()) + "': " + error.message()
        );
    }

    gameOptions.resourcePath = pathToUtf8(resolveProjectResourcePath(result));
    gameOptions.audio.missingDirectoriesAllowed =
        configuration.type == ProjectConfigurationType::Release;
    gameOptions.sprites.missingDirectoriesAllowed =
        configuration.type == ProjectConfigurationType::Release;

    return result;
}

NomadProject discoverNomadProject(const NomadPath& startPath) {
    const auto root = findProjectRoot(startPath);

    if (!root) {
        throw ProjectConfigurationException(
            "Could not find " + NomadString(NOMAD_PROJECT_FILE_NAME) +
            " from '" + pathToUtf8(startPath) + "' or any parent directory"
        );
    }

    return loadNomadProject(*root / NOMAD_PROJECT_FILE_NAME);
}

NomadPath resolveProjectResourcePath(const NomadProject& project) {
    return (project.root / project.configuration.resources.directory).lexically_normal();
}

NomadString serializeReleaseProject(const NomadProject& project) {
    const auto& configuration = project.configuration;

    const auto typedConfiguration =
        "schema = " + std::to_string(configuration.schema) + "\n"
        "\n"
        "[project]\n"
        "name = \"" + escapeTomlString(configuration.project.name) + "\"\n"
        "identifier = \"" + escapeTomlString(configuration.project.identifier) + "\"\n"
        "version = \"" + escapeTomlString(configuration.project.version) + "\"\n"
        "entry = \"" + escapeTomlString(project.gameOptions.entryFunction) + "\"\n"
        "\n"
        "[nomad]\n"
        "version = \"" + configuration.sdk.version.toString() + "\"\n" +
        serializeGameConfiguration(project.gameOptions) +
        serializeWindowConfiguration(project.gameOptions) +
        serializeAudioConfiguration(project.gameOptions.audio) +
        serializeSpriteConfiguration(project.gameOptions.sprites) +
        serializeTextConfiguration(project.gameOptions.text) +
        serializeScriptConfiguration(project.gameOptions.scripts);

    auto document = project.document;
    document.erase("project.executable");
    document.erase("resources");
    document.erase("package");
    for (const auto field : {
        "game.debug",
        "window.title",
        "window.resolution",
        "window.size",
        "window.fullscreen",
        "window.splash-image",
        "audio.directories",
        "sprites.directories",
        "sprites.max-texture-size",
        "sprites.padding",
        "text.directories",
        "scripts.directories"
    }) {
        document.erase(field);
    }
    document.merge(NomadConfiguration::parse(typedConfiguration));
    document.setStringArray("nomad.modules", configuration.sdk.modules);
    return document.serialize();
}

void validateNomadVersionCompatibility(
    const NomadVersion& requiredVersion,
    const NomadVersion& availableVersion
) {
    if (requiredVersion > availableVersion) {
        throw ProjectConfigurationException(
            "Project requires Nomad " + requiredVersion.toString() +
            ", but this CLI provides Nomad " + availableVersion.toString()
        );
    }
}

} // namespace nomad
