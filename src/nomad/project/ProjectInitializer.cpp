// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectInitializer.hpp>

#include <nomad/project/ProjectConfiguration.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/Version.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
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

void writeFile(const NomadPath& path, const NomadString& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);

    if (!output.is_open()) {
        throw ProjectInitializationError("Failed to create '" + pathToUtf8(path) + "'");
    }

    output << content;

    if (!output) {
        throw ProjectInitializationError("Failed to write '" + pathToUtf8(path) + "'");
    }
}

NomadString makeConfiguration(const NomadString& projectName, const NomadString& executable) {
    return
        "schema = 1\n"
        "\n"
        "[project]\n"
        "name = \"" + escapeTomlString(projectName) + "\"\n"
        "identifier = \"com.example." + executable + "\"\n"
        "version = \"0.1.0\"\n"
        "executable = \"" + executable + "\"\n"
        "entry = \"init\"\n"
        "\n"
        "[nomad]\n"
        "version = \"" + getNomadVersion().toString() + "\"\n"
        "\n"
        "[resources]\n"
        "directory = \"res\"\n"
        "\n"
        "[package]\n"
        "output = \"dist\"\n"
        "exclude = [\n"
        "    \"**/*.psd\",\n"
        "    \"**/*.kra\",\n"
        "    \"development/**\",\n"
        "]\n";
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

ProjectInitializationResult initializeProject(const NomadPath& destination) {
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
    const auto projectFile = root / NOMAD_PROJECT_FILE_NAME;
    const auto initFile = root / "res" / "scripts" / "init.nomad";

    for (const auto& path : {projectFile, initFile}) {
        if (std::filesystem::exists(path, error)) {
            throw ProjectInitializationError("Refusing to overwrite existing file '" + pathToUtf8(path) + "'");
        }

        if (error) {
            throw ProjectInitializationError("Failed to inspect '" + pathToUtf8(path) + "': " + error.message());
        }
    }

    auto projectTemporary = projectFile;
    projectTemporary += ".tmp";
    auto initTemporary = initFile;
    initTemporary += ".tmp";

    for (const auto& path : {projectTemporary, initTemporary}) {
        if (std::filesystem::exists(path, error)) {
            throw ProjectInitializationError("Temporary initialization file already exists: '" + pathToUtf8(path) + "'");
        }

        if (error) {
            throw ProjectInitializationError("Failed to inspect '" + pathToUtf8(path) + "': " + error.message());
        }
    }

    auto projectCreated = false;

    try {
        std::filesystem::create_directories(initFile.parent_path());
        writeFile(projectTemporary, makeConfiguration(projectName, executable));
        writeFile(initTemporary, "# Initialize the game.\n");
        std::filesystem::rename(projectTemporary, projectFile);
        projectCreated = true;
        std::filesystem::rename(initTemporary, initFile);
    } catch (...) {
        std::filesystem::remove(projectTemporary, error);
        std::filesystem::remove(initTemporary, error);

        if (projectCreated) {
            std::filesystem::remove(projectFile, error);
        }

        throw;
    }

    return ProjectInitializationResult{root, {projectFile, initFile}};
}

} // namespace nomad
