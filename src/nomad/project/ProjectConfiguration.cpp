// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/system/ModuleManifest.hpp>
#include <nomad/system/Path.hpp>

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
        throw NomadConfigurationException(
            "Configuration field '" + NomadString(tableName) + "." + NomadString(key) +
            "' must be a non-empty string"
        );
    }

    return *value;
}

const toml::table& requiredTable(const toml::table& document, const NomadStringView name) {
    const auto* table = document[name].as_table();

    if (table == nullptr) {
        throw NomadConfigurationException("Configuration table '[" + NomadString(name) + "]' is required");
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
        throw NomadConfigurationException(
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
        throw NomadConfigurationException(
            "Configuration field '" + NomadString(field) + "' must be a project-relative directory"
        );
    }
}

std::vector<NomadString> readExclusions(const toml::table& package) {
    const auto* exclusions = package["exclude"].as_array();

    if (exclusions == nullptr) {
        throw NomadConfigurationException("Configuration field 'package.exclude' must be an array of strings");
    }

    std::vector<NomadString> result;
    result.reserve(exclusions->size());

    for (const auto& exclusion : *exclusions) {
        const auto value = exclusion.value<NomadString>();

        if (!value || value->empty()) {
            throw NomadConfigurationException(
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
        throw NomadConfigurationException(
            "Failed to resolve project search path '" + pathToUtf8(startPath) + "': " + error.message()
        );
    }

    if (std::filesystem::is_regular_file(current, error)) {
        current = current.parent_path();
    } else if (error) {
        throw NomadConfigurationException(
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
            throw NomadConfigurationException(
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

NomadProject loadNomadProject(const NomadPath& projectFile) {
    toml::table document;

    std::ifstream input(projectFile, std::ios::binary);

    if (!input) {
        throw NomadConfigurationException(
            "Failed to open project configuration '" + pathToUtf8(projectFile) + "' for reading"
        );
    }

    try {
        document = toml::parse(input, pathToUtf8(projectFile));
    } catch (const toml::parse_error& error) {
        throw NomadConfigurationException(formatParseError(error));
    }

    const auto schema = document["schema"].value<NomadInteger>();

    if (!document["schema"].is_integer() || !schema) {
        throw NomadConfigurationException("Configuration field 'schema' must be an integer");
    }

    if (*schema != NOMAD_PROJECT_SCHEMA_VERSION) {
        throw NomadConfigurationException(
            "Unsupported project schema " + std::to_string(*schema) +
            "; expected " + std::to_string(NOMAD_PROJECT_SCHEMA_VERSION)
        );
    }

    const auto& project = requiredTable(document, "project");
    const auto& nomad = requiredTable(document, "nomad");
    const auto* package = optionalTable(document, "package");
    const auto hasExecutable = project.contains("executable");

    if (!hasExecutable && package != nullptr) {
        throw NomadConfigurationException(
            "Development configuration requires 'project.executable' when '[package]' is supplied"
        );
    }

    NomadProject result;
    std::ostringstream retainedDocument;
    retainedDocument << toml::toml_formatter{document};
    result.document = NomadConfiguration::parse(retainedDocument.str());
    auto& configuration = result.configuration;
    configuration.schema = *schema;
    configuration.type = hasExecutable
        ? ProjectConfigurationType::Development
        : ProjectConfigurationType::Release;
    configuration.project.name = requiredString(project, "project", "name");
    configuration.project.identifier = requiredString(project, "project", "identifier");
    configuration.project.version = requiredString(project, "project", "version");
    if (project.contains("entry")) {
        throw NomadConfigurationException("Configuration field 'project.entry' has moved to 'game.entry'");
    }
    const auto nomadVersion = requiredString(nomad, "nomad", "version");

    try {
        configuration.sdk.version = NomadVersion::parse(nomadVersion);
    } catch (const NomadVersionException& error) {
        throw NomadConfigurationException(
            "Configuration field 'nomad.version' is invalid: " + NomadString(error.what())
        );
    }

    {
        configuration.sdk.modules = result.document.requireStringArray("nomad.modules");

        std::vector<NomadString> names;
        for (const auto& name : configuration.sdk.modules) {
            if (!isValidModuleName(name)) {
                throw NomadConfigurationException(
                    "Configuration field 'nomad.modules' must contain only logical module names"
                );
            }
            if (std::ranges::find(names, name) != names.end()) {
                throw NomadConfigurationException(
                    "Configuration field 'nomad.modules' contains duplicate module '" + name + "'"
                );
            }
            names.push_back(name);
        }
    }

    if (configuration.type == ProjectConfigurationType::Development) {
        configuration.project.executable = requiredString(project, "project", "executable");
        if (package != nullptr) {
            configuration.package.output = pathFromUtf8(optionalString(
                *package, "package", "output", pathToUtf8(configuration.package.output)));
            if (package->contains("exclude")) {
                configuration.package.exclude = readExclusions(*package);
            }
        }
        validateExecutable(configuration.project.executable);
        validateProjectRelativePath(configuration.package.output, "package.output", false);
    } else {
        configuration.package.output.clear();
        configuration.package.exclude.clear();
    }

    std::error_code error;
    result.root = std::filesystem::absolute(projectFile.parent_path(), error);

    if (error) {
        throw NomadConfigurationException(
            "Failed to resolve project root '" + pathToUtf8(projectFile.parent_path()) + "': " + error.message()
        );
    }

    return result;
}

NomadProject discoverNomadProject(const NomadPath& startPath) {
    const auto root = findProjectRoot(startPath);

    if (!root) {
        throw NomadConfigurationException(
            "Could not find " + NomadString(NOMAD_PROJECT_FILE_NAME) +
            " from '" + pathToUtf8(startPath) + "' or any parent directory"
        );
    }

    return loadNomadProject(*root / NOMAD_PROJECT_FILE_NAME);
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
        "\n"
        "[nomad]\n"
        "version = \"" + configuration.sdk.version.toString() + "\"\n";

    auto document = project.document;
    document.erase("project.executable");
    document.erase("package");
    document.merge(NomadConfiguration::parse(typedConfiguration));
    document.setStringArray("nomad.modules", configuration.sdk.modules);
    return document.serialize();
}

void validateNomadVersionCompatibility(
    const NomadVersion& requiredVersion,
    const NomadVersion& availableVersion
) {
    if (requiredVersion > availableVersion) {
        throw NomadConfigurationException(
            "Project requires Nomad " + requiredVersion.toString() +
            ", but this CLI provides Nomad " + availableVersion.toString()
        );
    }
}

} // namespace nomad
