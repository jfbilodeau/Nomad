// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectConfiguration.hpp>

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
        throw ProjectConfigurationError(
            "Configuration field '" + NomadString(tableName) + "." + NomadString(key) +
            "' must be a non-empty string"
        );
    }

    return *value;
}

const toml::table& requiredTable(const toml::table& document, const NomadStringView name) {
    const auto* table = document[name].as_table();

    if (table == nullptr) {
        throw ProjectConfigurationError("Configuration table '[" + NomadString(name) + "]' is required");
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
        throw ProjectConfigurationError(
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
        throw ProjectConfigurationError(
            "Configuration field '" + NomadString(field) + "' must be a project-relative directory"
        );
    }
}

std::vector<NomadString> readExclusions(const toml::table& package) {
    const auto* exclusions = package["exclude"].as_array();

    if (exclusions == nullptr) {
        throw ProjectConfigurationError("Configuration field 'package.exclude' must be an array of strings");
    }

    std::vector<NomadString> result;
    result.reserve(exclusions->size());

    for (const auto& exclusion : *exclusions) {
        const auto value = exclusion.value<NomadString>();

        if (!value || value->empty()) {
            throw ProjectConfigurationError(
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

std::vector<NomadPath> readSpriteDirectories(const toml::table& sprites) {
    const auto* directories = sprites["directories"].as_array();

    if (directories == nullptr) {
        throw ProjectConfigurationError("Configuration field 'sprites.directories' must be an array of strings");
    }

    std::vector<NomadPath> result;
    std::vector<NomadString> normalizedKeys;
    result.reserve(directories->size());
    normalizedKeys.reserve(directories->size());

    for (const auto& directory : *directories) {
        const auto value = directory.value<NomadString>();

        if (!value || value->empty()) {
            throw ProjectConfigurationError(
                "Configuration field 'sprites.directories' must contain only non-empty strings"
            );
        }

        auto normalizedValue = *value;
        std::ranges::replace(normalizedValue, '\\', '/');
        auto path = pathFromUtf8(normalizedValue).lexically_normal();

        if (!path.empty() && !path.has_filename()) {
            path = path.parent_path();
        }

        validateProjectRelativePath(path, "sprites.directories", false);
        const auto key = normalizedDirectoryKey(path);

        for (const auto& existing : normalizedKeys) {
            if (directoriesOverlap(existing, key)) {
                throw ProjectConfigurationError(
                    "Configuration field 'sprites.directories' contains duplicate or overlapping directories"
                );
            }
        }

        normalizedKeys.push_back(key);
        result.push_back(std::move(path));
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
        throw ProjectConfigurationError(
            "Configuration field '" + field + "' must be a resource-relative file"
        );
    }

    return path;
}

std::pair<NomadInteger, NomadInteger> readResolution(const toml::table& window) {
    const auto* resolution = window["resolution"].as_array();

    if (resolution == nullptr || resolution->size() != 2) {
        throw ProjectConfigurationError(
            "Configuration field 'window.resolution' must be an array of two positive integers"
        );
    }

    const auto width = (*resolution)[0].value<NomadInteger>();
    const auto height = (*resolution)[1].value<NomadInteger>();

    if (
        !(*resolution)[0].is_integer() ||
        !(*resolution)[1].is_integer() ||
        !width ||
        !height ||
        *width <= 0 ||
        *height <= 0
    ) {
        throw ProjectConfigurationError(
            "Configuration field 'window.resolution' must be an array of two positive integers"
        );
    }

    return {*width, *height};
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
        throw ProjectConfigurationError("Configuration field '" + field + "' must be an integer");
    }

    if (*value < minimum) {
        throw ProjectConfigurationError(
            "Configuration field '" + field + "' must be at least " + std::to_string(minimum)
        );
    }

    return *value;
}

NomadString serializeSpriteConfiguration(const SpriteConfiguration& configuration) {
    if (
        !configuration.directoriesExplicitlyConfigured &&
        !configuration.maxTextureSizeExplicitlyConfigured &&
        !configuration.paddingExplicitlyConfigured
    ) {
        return {};
    }

    NomadString result = "\n[sprites]\n";

    if (configuration.directoriesExplicitlyConfigured) {
        result += "directories = [";

        for (NomadIndex index = 0; index < configuration.directories.size(); ++index) {
            if (index != 0) {
                result += ", ";
            }

            result += "\"" + escapeTomlString(pathToGenericUtf8(configuration.directories[index])) + "\"";
        }

        result += "]\n";
    }

    if (configuration.maxTextureSizeExplicitlyConfigured) {
        result += "max-texture-size = " + std::to_string(configuration.maxTextureSize) + "\n";
    }

    if (configuration.paddingExplicitlyConfigured) {
        result += "padding = " + std::to_string(configuration.padding) + "\n";
    }

    return result;
}

NomadString serializeWindowConfiguration(const WindowConfiguration& configuration) {
    if (
        !configuration.titleExplicitlyConfigured &&
        !configuration.resolutionExplicitlyConfigured &&
        !configuration.splashImage
    ) {
        return {};
    }

    NomadString result = "\n[window]\n";

    if (configuration.titleExplicitlyConfigured) {
        result += "title = \"" + escapeTomlString(configuration.title) + "\"\n";
    }
    if (configuration.resolutionExplicitlyConfigured) {
        result +=
            "resolution = [" + std::to_string(configuration.resolutionWidth) + ", " +
            std::to_string(configuration.resolutionHeight) + "]\n";
    }
    if (configuration.splashImage) {
        result +=
            "splash-image = \"" +
            escapeTomlString(pathToGenericUtf8(*configuration.splashImage)) + "\"\n";
    }

    return result;
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
        throw ProjectConfigurationError(
            "Failed to resolve project search path '" + pathToUtf8(startPath) + "': " + error.message()
        );
    }

    if (std::filesystem::is_regular_file(current, error)) {
        current = current.parent_path();
    } else if (error) {
        throw ProjectConfigurationError(
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
            throw ProjectConfigurationError(
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

ProjectConfiguration loadProjectConfiguration(const NomadPath& projectFile) {
    toml::table document;

    std::ifstream input(projectFile, std::ios::binary);

    if (!input) {
        throw ProjectConfigurationError(
            "Failed to open project configuration '" + pathToUtf8(projectFile) + "' for reading"
        );
    }

    try {
        document = toml::parse(input, pathToUtf8(projectFile));
    } catch (const toml::parse_error& error) {
        throw ProjectConfigurationError(formatParseError(error));
    }

    const auto schema = document["schema"].value<NomadInteger>();

    if (!schema) {
        throw ProjectConfigurationError("Configuration field 'schema' must be an integer");
    }

    if (*schema != NOMAD_PROJECT_SCHEMA_VERSION) {
        throw ProjectConfigurationError(
            "Unsupported project schema " + std::to_string(*schema) +
            "; expected " + std::to_string(NOMAD_PROJECT_SCHEMA_VERSION)
        );
    }

    const auto& project = requiredTable(document, "project");
    const auto& nomad = requiredTable(document, "nomad");
    const auto* resources = optionalTable(document, "resources");
    const auto* window = optionalTable(document, "window");
    const auto* sprites = optionalTable(document, "sprites");
    const auto* package = optionalTable(document, "package");
    const auto hasExecutable = project.contains("executable");

    if (!hasExecutable && (resources != nullptr || package != nullptr)) {
        throw ProjectConfigurationError(
            "Development configuration requires 'project.executable' when '[resources]' or '[package]' is supplied"
        );
    }

    ProjectConfiguration configuration;
    configuration.schema = *schema;
    configuration.type = hasExecutable
        ? ProjectConfigurationType::Development
        : ProjectConfigurationType::Release;
    configuration.project.name = requiredString(project, "project", "name");
    configuration.project.identifier = requiredString(project, "project", "identifier");
    configuration.project.version = requiredString(project, "project", "version");
    configuration.project.entry = optionalString(project, "project", "entry", configuration.project.entry);
    configuration.window.title = configuration.project.name;
    const auto nomadVersion = requiredString(nomad, "nomad", "version");

    try {
        configuration.nomad.version = NomadVersion::parse(nomadVersion);
    } catch (const NomadVersionError& error) {
        throw ProjectConfigurationError(
            "Configuration field 'nomad.version' is invalid: " + NomadString(error.what())
        );
    }

    if (window != nullptr && window->contains("title")) {
        configuration.window.title = requiredString(*window, "window", "title");
        configuration.window.titleExplicitlyConfigured = true;
    }
    if (window != nullptr && window->contains("resolution")) {
        const auto [width, height] = readResolution(*window);
        configuration.window.resolutionWidth = width;
        configuration.window.resolutionHeight = height;
        configuration.window.resolutionExplicitlyConfigured = true;
    }
    if (window != nullptr && window->contains("splash-image")) {
        configuration.window.splashImage =
            readResourceRelativeFile(*window, "window", "splash-image");
    }

    if (sprites != nullptr && sprites->contains("directories")) {
        configuration.sprites.directories = readSpriteDirectories(*sprites);
        configuration.sprites.directoriesExplicitlyConfigured = true;
    }
    if (sprites != nullptr && sprites->contains("max-texture-size")) {
        configuration.sprites.maxTextureSize = readSpriteInteger(*sprites, "max-texture-size", 1);
        configuration.sprites.maxTextureSizeExplicitlyConfigured = true;
    }
    if (sprites != nullptr && sprites->contains("padding")) {
        configuration.sprites.padding = readSpriteInteger(*sprites, "padding", 0);
        configuration.sprites.paddingExplicitlyConfigured = true;
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

    if (configuration.project.entry.empty()) {
        throw ProjectConfigurationError("Configuration field 'project.entry' must be a non-empty string");
    }

    std::error_code error;
    configuration.root = std::filesystem::absolute(projectFile.parent_path(), error);

    if (error) {
        throw ProjectConfigurationError(
            "Failed to resolve project root '" + pathToUtf8(projectFile.parent_path()) + "': " + error.message()
        );
    }

    return configuration;
}

ProjectConfiguration discoverProjectConfiguration(const NomadPath& startPath) {
    const auto root = findProjectRoot(startPath);

    if (!root) {
        throw ProjectConfigurationError(
            "Could not find " + NomadString(NOMAD_PROJECT_FILE_NAME) +
            " from '" + pathToUtf8(startPath) + "' or any parent directory"
        );
    }

    return loadProjectConfiguration(*root / NOMAD_PROJECT_FILE_NAME);
}

NomadPath resolveProjectResourcePath(const ProjectConfiguration& configuration) {
    return (configuration.root / configuration.resources.directory).lexically_normal();
}

NomadString serializeReleaseProjectConfiguration(const ProjectConfiguration& configuration) {
    return
        "schema = " + std::to_string(configuration.schema) + "\n"
        "\n"
        "[project]\n"
        "name = \"" + escapeTomlString(configuration.project.name) + "\"\n"
        "identifier = \"" + escapeTomlString(configuration.project.identifier) + "\"\n"
        "version = \"" + escapeTomlString(configuration.project.version) + "\"\n"
        "entry = \"" + escapeTomlString(configuration.project.entry) + "\"\n"
        "\n"
        "[nomad]\n"
        "version = \"" + configuration.nomad.version.toString() + "\"\n" +
        serializeWindowConfiguration(configuration.window) +
        serializeSpriteConfiguration(configuration.sprites);
}

void validateNomadVersionCompatibility(
    const NomadVersion& requiredVersion,
    const NomadVersion& availableVersion
) {
    if (requiredVersion > availableVersion) {
        throw ProjectConfigurationError(
            "Project requires Nomad " + requiredVersion.toString() +
            ", but this CLI provides Nomad " + availableVersion.toString()
        );
    }
}

} // namespace nomad
