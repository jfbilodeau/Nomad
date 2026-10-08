// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/system/Path.hpp>

#include <toml++/toml.hpp>

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

NomadPath requiredPath(
    const toml::table& table,
    const NomadStringView tableName,
    const NomadStringView key
) {
    return pathFromUtf8(requiredString(table, tableName, key));
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
    const auto& resources = requiredTable(document, "resources");
    const auto& package = requiredTable(document, "package");

    ProjectConfiguration configuration;
    configuration.schema = *schema;
    configuration.project.name = requiredString(project, "project", "name");
    configuration.project.identifier = requiredString(project, "project", "identifier");
    configuration.project.version = requiredString(project, "project", "version");
    configuration.project.executable = requiredString(project, "project", "executable");
    configuration.project.entry = project["entry"].value_or<NomadString>("init");
    const auto nomadVersion = requiredString(nomad, "nomad", "version");

    try {
        configuration.nomad.version = NomadVersion::parse(nomadVersion);
    } catch (const NomadVersionError& error) {
        throw ProjectConfigurationError(
            "Configuration field 'nomad.version' is invalid: " + NomadString(error.what())
        );
    }
    configuration.resources.directory = requiredPath(resources, "resources", "directory");
    configuration.package.output = requiredPath(package, "package", "output");
    configuration.package.exclude = readExclusions(package);

    if (configuration.project.entry.empty()) {
        throw ProjectConfigurationError("Configuration field 'project.entry' must be a non-empty string");
    }

    validateExecutable(configuration.project.executable);

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
