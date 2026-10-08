// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectInitializer.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/Version.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

namespace nomad {

namespace {

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

NomadString readFile(const NomadPath& path) {
    std::ifstream input(path, std::ios::binary);

    if (!input.is_open()) {
        throw ProjectInitializationError("Failed to open project template file '" + pathToUtf8(path) + "'");
    }

    NomadString content{
        std::istreambuf_iterator<NomadChar>(input),
        std::istreambuf_iterator<NomadChar>()
    };

    if (input.bad()) {
        throw ProjectInitializationError("Failed to read project template file '" + pathToUtf8(path) + "'");
    }

    return content;
}

void writeFile(const NomadPath& path, const NomadStringView content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);

    if (!output.is_open()) {
        throw ProjectInitializationError("Failed to create '" + pathToUtf8(path) + "'");
    }

    output << content;

    if (!output) {
        throw ProjectInitializationError("Failed to write '" + pathToUtf8(path) + "'");
    }
}

NomadString renderTemplate(
    const NomadStringView content,
    const std::map<NomadString, NomadString, std::less<>>& variables,
    const NomadPath& source
) {
    NomadString rendered;
    auto position = NomadIndex{0};

    while (position < content.size()) {
        const auto opening = content.find("{{", position);
        const auto closing = content.find("}}", position);

        if (closing != NomadStringView::npos && (opening == NomadStringView::npos || closing < opening)) {
            throw ProjectInitializationError(
                "Unexpected '}}' in project template file '" + pathToUtf8(source) + "'"
            );
        }

        if (opening == NomadStringView::npos) {
            rendered.append(content.substr(position));
            break;
        }

        rendered.append(content.substr(position, opening - position));
        const auto end = content.find("}}", opening + 2U);

        if (end == NomadStringView::npos) {
            throw ProjectInitializationError(
                "Unterminated placeholder in project template file '" + pathToUtf8(source) + "'"
            );
        }

        const auto name = content.substr(opening + 2U, end - opening - 2U);
        const auto variable = variables.find(name);

        if (variable == variables.end()) {
            throw ProjectInitializationError(
                "Unknown placeholder '{{" + NomadString(name) + "}}' in project template file '" +
                pathToUtf8(source) + "'"
            );
        }

        rendered += variable->second;
        position = end + 2U;
    }

    return rendered;
}

struct ProjectTemplateFile {
    NomadPath source;
    NomadPath destination;
    bool render;
};

std::vector<ProjectTemplateFile> collectTemplateFiles(
    const NomadPath& templateDirectory,
    const NomadPath& destination
) {
    std::error_code error;

    if (!std::filesystem::is_directory(templateDirectory, error)) {
        if (error) {
            throw ProjectInitializationError(
                "Failed to inspect project template directory '" + pathToUtf8(templateDirectory) +
                "': " + error.message()
            );
        }

        throw ProjectInitializationError(
            "Project template directory does not exist: '" + pathToUtf8(templateDirectory) + "'"
        );
    }

    std::vector<ProjectTemplateFile> files;
    std::set<NomadPath> destinations;
    std::filesystem::recursive_directory_iterator iterator(templateDirectory, error);
    const std::filesystem::recursive_directory_iterator end;

    while (iterator != end) {
        if (error) {
            throw ProjectInitializationError(
                "Failed to read project template directory '" + pathToUtf8(templateDirectory) +
                "': " + error.message()
            );
        }

        const auto& entry = *iterator;

        if (entry.is_symlink(error)) {
            throw ProjectInitializationError(
                "Project templates must not contain symbolic links: '" + pathToUtf8(entry.path()) + "'"
            );
        }

        if (error) {
            throw ProjectInitializationError(
                "Failed to inspect project template entry '" + pathToUtf8(entry.path()) +
                "': " + error.message()
            );
        }

        if (entry.is_regular_file(error)) {
            auto relative = std::filesystem::relative(entry.path(), templateDirectory, error);

            if (error) {
                throw ProjectInitializationError(
                    "Failed to resolve project template entry '" + pathToUtf8(entry.path()) +
                    "': " + error.message()
                );
            }

            const auto render = relative.extension() == ".in";

            if (render) {
                relative.replace_extension();
            }

            auto output = destination / relative;

            if (!destinations.insert(output).second) {
                throw ProjectInitializationError(
                    "Project template contains multiple files for '" + pathToUtf8(output) + "'"
                );
            }

            files.push_back(ProjectTemplateFile{entry.path(), std::move(output), render});
        } else if (!entry.is_directory(error)) {
            throw ProjectInitializationError(
                "Unsupported project template entry: '" + pathToUtf8(entry.path()) + "'"
            );
        }

        if (error) {
            throw ProjectInitializationError(
                "Failed to inspect project template entry '" + pathToUtf8(entry.path()) +
                "': " + error.message()
            );
        }

        iterator.increment(error);
    }

    std::ranges::sort(files, {}, [](const ProjectTemplateFile& file) {
        return file.destination;
    });
    return files;
}

void createDirectories(const NomadPath& directory, std::vector<NomadPath>& createdDirectories) {
    std::vector<NomadPath> missing;
    auto current = directory;
    std::error_code error;

    while (!std::filesystem::exists(current, error)) {
        if (error) {
            throw ProjectInitializationError(
                "Failed to inspect project directory '" + pathToUtf8(current) + "': " + error.message()
            );
        }

        missing.push_back(current);
        const auto parent = current.parent_path();

        if (parent == current || parent.empty()) {
            throw ProjectInitializationError(
                "Could not find an existing parent for project directory '" + pathToUtf8(directory) + "'"
            );
        }

        current = parent;
    }

    if (error) {
        throw ProjectInitializationError(
            "Failed to inspect project directory '" + pathToUtf8(current) + "': " + error.message()
        );
    }

    if (!std::filesystem::is_directory(current, error)) {
        throw ProjectInitializationError(
            "Project directory parent is not a directory: '" + pathToUtf8(current) + "'"
        );
    }

    if (error) {
        throw ProjectInitializationError(
            "Failed to inspect project directory '" + pathToUtf8(current) + "': " + error.message()
        );
    }

    for (auto path = missing.rbegin(); path != missing.rend(); ++path) {
        if (!std::filesystem::create_directory(*path, error) || error) {
            throw ProjectInitializationError(
                "Failed to create project directory '" + pathToUtf8(*path) + "': " + error.message()
            );
        }

        createdDirectories.push_back(*path);
    }
}

} // namespace

NomadString makeProjectExecutableName(const NomadStringView projectName) {
    NomadString executable;
    auto separatorPending = false;

    for (const auto character : projectName) {
        const auto byte = static_cast<unsigned char>(character);
        // Executable names are restricted to ASCII so they remain portable across platforms.
        const auto isAsciiDigit = byte >= '0' && byte <= '9';
        const auto isAsciiLetter = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');

        if (isAsciiDigit || isAsciiLetter) {
            if (separatorPending && !executable.empty()) {
                executable += '-';
            }

            executable += static_cast<NomadChar>(std::tolower(byte));
            separatorPending = false;
        } else {
            separatorPending = true;
        }
    }

    if (executable.empty()) {
        throw ProjectInitializationError(
            "Project name must contain at least one ASCII letter or number to derive an executable name"
        );
    }

    return executable;
}

ProjectInitializationResult initializeProject(
    const NomadPath& destination,
    const NomadPath& templateDirectory
) {
    std::error_code error;
    auto root = std::filesystem::absolute(destination, error).lexically_normal();

    if (error) {
        throw ProjectInitializationError(
            "Failed to resolve project directory '" + pathToUtf8(destination) + "': " + error.message()
        );
    }

    if (std::filesystem::exists(root, error) && !std::filesystem::is_directory(root, error)) {
        throw ProjectInitializationError("Project destination is not a directory: '" + pathToUtf8(root) + "'");
    }

    if (error) {
        throw ProjectInitializationError(
            "Failed to inspect project directory '" + pathToUtf8(root) + "': " + error.message()
        );
    }

    const auto projectName = pathToUtf8(root.filename());
    const auto executable = makeProjectExecutableName(projectName);
    const auto templateRoot = std::filesystem::absolute(templateDirectory, error).lexically_normal();

    if (error) {
        throw ProjectInitializationError(
            "Failed to resolve project template directory '" + pathToUtf8(templateDirectory) +
            "': " + error.message()
        );
    }

    const auto files = collectTemplateFiles(templateRoot, root);
    const std::map<NomadString, NomadString, std::less<>> variables{
        {"nomad_version", getNomadVersion().toString()},
        {"project_executable", executable},
        {"project_name", projectName},
        {"project_name_toml", escapeTomlString(projectName)}
    };

    for (const auto& file : files) {
        if (std::filesystem::exists(file.destination, error)) {
            throw ProjectInitializationError(
                "Refusing to overwrite existing file '" + pathToUtf8(file.destination) + "'"
            );
        }

        if (error) {
            throw ProjectInitializationError(
                "Failed to inspect '" + pathToUtf8(file.destination) + "': " + error.message()
            );
        }

        auto temporary = file.destination;
        temporary += ".tmp";

        if (std::filesystem::exists(temporary, error)) {
            throw ProjectInitializationError(
                "Temporary initialization file already exists: '" + pathToUtf8(temporary) + "'"
            );
        }

        if (error) {
            throw ProjectInitializationError(
                "Failed to inspect '" + pathToUtf8(temporary) + "': " + error.message()
            );
        }
    }

    std::vector<NomadPath> createdFiles;
    std::vector<NomadPath> createdDirectories;
    std::vector<NomadPath> temporaryFiles;

    try {
        for (const auto& file : files) {
            createDirectories(file.destination.parent_path(), createdDirectories);

            auto temporary = file.destination;
            temporary += ".tmp";
            temporaryFiles.push_back(temporary);

            if (file.render) {
                writeFile(temporary, renderTemplate(readFile(file.source), variables, file.source));
            } else {
                std::filesystem::copy_file(file.source, temporary);
            }

            std::filesystem::rename(temporary, file.destination);
            temporaryFiles.pop_back();
            createdFiles.push_back(file.destination);
        }
    } catch (...) {
        for (const auto& path : temporaryFiles) {
            std::filesystem::remove(path, error);
        }

        for (auto path = createdFiles.rbegin(); path != createdFiles.rend(); ++path) {
            std::filesystem::remove(*path, error);
        }

        for (auto path = createdDirectories.rbegin(); path != createdDirectories.rend(); ++path) {
            std::filesystem::remove(*path, error);
        }

        throw;
    }

    return ProjectInitializationResult{root, std::move(createdFiles)};
}

} // namespace nomad
