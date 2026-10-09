// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectPackager.hpp>

#include <nomad/system/Path.hpp>

#include <boost/json.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <system_error>
#include <vector>

namespace nomad {

namespace {

[[noreturn]] void raiseFilesystemError(
    const NomadStringView operation,
    const NomadPath& path,
    const std::error_code& error
) {
    throw ProjectPackagingError(
        NomadString(operation) + " '" + pathToUtf8(path) + "': " + error.message()
    );
}

bool isWithin(const NomadPath& path, const NomadPath& parent) {
    const auto relative = path.lexically_relative(parent);
    return
        !relative.empty() &&
        *relative.begin() != ".." &&
        !relative.is_absolute();
}

bool isProjectRelativeDirectory(const NomadPath& path) {
    if (path.empty() || path.is_absolute()) {
        return false;
    }

    const auto normalized = path.lexically_normal();
    return normalized != "." && *normalized.begin() != "..";
}

NomadString normalizePattern(const NomadStringView pattern) {
    NomadString normalized(pattern);
    std::ranges::replace(normalized, '\\', '/');
    return normalized;
}

bool matchesGlob(const NomadStringView pattern, const NomadStringView path) {
    const auto columns = path.size() + 1U;
    std::vector<int8_t> memo((pattern.size() + 1U) * columns, -1);

    const std::function<bool(NomadIndex, NomadIndex)> match =
        [&](const NomadIndex patternIndex, const NomadIndex pathIndex) {
            auto& cached = memo[patternIndex * columns + pathIndex];

            if (cached >= 0) {
                return cached != 0;
            }

            bool result;

            if (patternIndex == pattern.size()) {
                result = pathIndex == path.size();
            } else if (
                pattern[patternIndex] == '*' &&
                patternIndex + 1U < pattern.size() &&
                pattern[patternIndex + 1U] == '*'
            ) {
                auto next = patternIndex + 2U;

                while (next < pattern.size() && pattern[next] == '*') {
                    ++next;
                }

                if (next < pattern.size() && pattern[next] == '/') {
                    result =
                        match(next + 1U, pathIndex) ||
                        (pathIndex < path.size() && match(patternIndex, pathIndex + 1U));
                } else {
                    result =
                        match(next, pathIndex) ||
                        (pathIndex < path.size() && match(patternIndex, pathIndex + 1U));
                }
            } else if (pattern[patternIndex] == '*') {
                result =
                    match(patternIndex + 1U, pathIndex) ||
                    (
                        pathIndex < path.size() &&
                        path[pathIndex] != '/' &&
                        match(patternIndex, pathIndex + 1U)
                    );
            } else if (pattern[patternIndex] == '?') {
                result =
                    pathIndex < path.size() &&
                    path[pathIndex] != '/' &&
                    match(patternIndex + 1U, pathIndex + 1U);
            } else {
                result =
                    pathIndex < path.size() &&
                    pattern[patternIndex] == path[pathIndex] &&
                    match(patternIndex + 1U, pathIndex + 1U);
            }

            cached = static_cast<int8_t>(result);
            return result;
        };

    return match(0U, 0U);
}

bool isExcluded(const NomadPath& relativePath, const std::vector<NomadString>& exclusions) {
    const auto path = pathToGenericUtf8(relativePath);

    return std::ranges::any_of(exclusions, [&](const NomadStringView exclusion) {
        return matchesGlob(normalizePattern(exclusion), path);
    });
}

void createDirectories(const NomadPath& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);

    if (error) {
        raiseFilesystemError("Failed to create directory", directory, error);
    }
}

void copyFile(const NomadPath& source, const NomadPath& destination) {
    createDirectories(destination.parent_path());
    std::error_code error;
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);

    if (error) {
        raiseFilesystemError("Failed to copy file to", destination, error);
    }
}

void writeReleaseManifest(const ProjectConfiguration& configuration, const NomadPath& destination) {
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);

    if (!output.is_open()) {
        throw ProjectPackagingError("Failed to create release manifest '" + pathToUtf8(destination) + "'");
    }

    output << serializeReleaseProjectConfiguration(configuration);

    if (!output) {
        throw ProjectPackagingError("Failed to write release manifest '" + pathToUtf8(destination) + "'");
    }
}

std::vector<NomadPath> copyResources(
    const ProjectConfiguration& configuration,
    const NomadPath& staging,
    const bool dryRun
) {
    const auto resources = resolveProjectResourcePath(configuration);
    std::error_code error;

    if (!std::filesystem::is_directory(resources, error)) {
        if (error) {
            raiseFilesystemError("Failed to inspect resource directory", resources, error);
        }

        throw ProjectPackagingError("Resource directory does not exist: '" + pathToUtf8(resources) + "'");
    }

    std::vector<NomadPath> files;
    std::filesystem::recursive_directory_iterator iterator(resources, error);
    const std::filesystem::recursive_directory_iterator end;

    while (iterator != end) {
        if (error) {
            raiseFilesystemError("Failed to read resource directory", resources, error);
        }

        const auto& entry = *iterator;

        if (entry.is_symlink(error)) {
            throw ProjectPackagingError(
                "Project resources must not contain symbolic links: '" + pathToUtf8(entry.path()) + "'"
            );
        }

        if (error) {
            raiseFilesystemError("Failed to inspect resource", entry.path(), error);
        }

        if (entry.is_regular_file(error)) {
            const auto relative = std::filesystem::relative(entry.path(), resources, error);

            if (error) {
                raiseFilesystemError("Failed to resolve resource", entry.path(), error);
            }

            if (!isExcluded(relative, configuration.package.exclude)) {
                const auto destination = staging / "res" / relative;

                if (!dryRun) {
                    copyFile(entry.path(), destination);
                }

                files.push_back(destination);
            }
        } else if (!entry.is_directory(error)) {
            throw ProjectPackagingError("Unsupported project resource: '" + pathToUtf8(entry.path()) + "'");
        }

        if (error) {
            raiseFilesystemError("Failed to inspect resource", entry.path(), error);
        }

        iterator.increment(error);
    }

    return files;
}

struct RuntimeBundleFile {
    NomadPath path;
    NomadString role;
};

struct RuntimeBundle {
    NomadString target;
    std::vector<RuntimeBundleFile> files;
};

RuntimeBundle loadRuntimeBundle(const ProjectConfiguration& configuration, const NomadPath& directory) {
    const auto manifest = directory / "runtime.json";
    std::error_code manifestError;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(manifest, manifestError))) {
        throw ProjectPackagingError("Runtime manifest must not be a symbolic link");
    }
    std::ifstream input(manifest, std::ios::binary);
    if (!input) {
        throw ProjectPackagingError("Failed to open runtime manifest '" + pathToUtf8(manifest) + "'");
    }
    const NomadString text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) {
        throw ProjectPackagingError("Failed to read runtime manifest '" + pathToUtf8(manifest) + "'");
    }
    boost::system::error_code parseError;
    const auto value = boost::json::parse(text, parseError);
    if (parseError || !value.is_object()) {
        throw ProjectPackagingError("Invalid runtime manifest JSON: '" + pathToUtf8(manifest) + "'");
    }
    const auto& object = value.as_object();
    const auto* schema = object.if_contains("schema");
    const auto* version = object.if_contains("version");
    const auto* target = object.if_contains("target");
    const auto* entries = object.if_contains("files");
    if (!schema || !schema->is_int64() || schema->as_int64() != 1 ||
        !version || !version->is_string() || !target || !target->is_string() ||
        !entries || !entries->is_array()) {
        throw ProjectPackagingError("Runtime manifest requires schema 1, version, target, and files");
    }
    if (version->as_string() != configuration.nomad.version.toString()) {
        throw ProjectPackagingError("Runtime bundle version does not match the project's required Nomad version");
    }
    RuntimeBundle bundle{NomadString(target->as_string()), {}};
    if (bundle.target.empty() || std::ranges::any_of(bundle.target, [](const unsigned char character) {
        return !(character >= 'a' && character <= 'z') && !(character >= '0' && character <= '9') && character != '-';
    })) {
        throw ProjectPackagingError("Runtime manifest target must contain only lowercase letters, digits, and hyphens");
    }
    auto runtimeCount = 0;
    auto licenseCount = 0;
    std::set<NomadString> paths;
    for (const auto& entry : entries->as_array()) {
        if (!entry.is_object()) {
            throw ProjectPackagingError("Runtime manifest file entries must be objects");
        }
        const auto* pathValue = entry.as_object().if_contains("path");
        const auto* roleValue = entry.as_object().if_contains("role");
        if (!pathValue || !pathValue->is_string() || !roleValue || !roleValue->is_string()) {
            throw ProjectPackagingError("Runtime manifest files require string path and role fields");
        }
        const NomadString name(pathValue->as_string());
        const auto path = pathFromUtf8(name);
        if (name.empty() || name.find_first_of("\\:") != NomadString::npos ||
            std::ranges::any_of(name, [](const unsigned char character) { return character < 32; }) ||
            pathToGenericUtf8(path.lexically_normal()) != name ||
            path.is_absolute() || path.has_root_name() || std::ranges::any_of(path, [](const auto& part) {
                return part == ".." || part == ".";
            }) || !paths.insert(name).second) {
            throw ProjectPackagingError("Unsafe or duplicate runtime manifest path: '" + name + "'");
        }
        const NomadString role(roleValue->as_string());
        if (role == "runtime") {
            ++runtimeCount;
            if (path.has_parent_path()) {
                throw ProjectPackagingError("Runtime executable must be at the bundle root");
            }
        } else if (role == "license") {
            ++licenseCount;
        } else if (role != "library") {
            throw ProjectPackagingError("Unknown runtime manifest file role: '" + role + "'");
        }
        auto current = directory;
        for (const auto& part : path) {
            current /= part;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(current, error);
            if (error || std::filesystem::is_symlink(status)) {
                throw ProjectPackagingError("Missing or symbolic-linked runtime file: '" + name + "'");
            }
        }
        if (!std::filesystem::is_regular_file(directory / path)) {
            throw ProjectPackagingError("Declared runtime file is not a regular file: '" + name + "'");
        }
        bundle.files.push_back({path, role});
    }
    if (runtimeCount != 1 || licenseCount == 0) {
        throw ProjectPackagingError("Runtime manifest must declare exactly one runtime executable and at least one license");
    }
    return bundle;
}

std::vector<NomadPath> copyRuntime(
    const ProjectConfiguration& configuration,
    const NomadPath& runtimeDirectory,
    const RuntimeBundle& bundle,
    const NomadPath& staging,
    const bool dryRun
) {
    std::vector<NomadPath> files;
    std::set<NomadPath> destinations{"nomad.toml"};
    for (const auto& entry : bundle.files) {
        auto destinationName = entry.path;
        if (entry.role == "runtime") {
            destinationName = pathFromUtf8(configuration.project.executable);
            destinationName += entry.path.extension();
        }
        if (!destinations.insert(destinationName).second || *destinationName.begin() == "res") {
            throw ProjectPackagingError("Runtime file collides with a package entry: '" + pathToUtf8(destinationName) + "'");
        }
        const auto destination = staging / destinationName;
        if (!dryRun) {
            copyFile(runtimeDirectory / entry.path, destination);
        }
        files.push_back(destination);
    }
    return files;
}

void checkArchive(const int result, archive* writer) {
    if (result != ARCHIVE_OK) {
        const auto* message = archive_error_string(writer);
        throw ProjectPackagingError("Failed to write ZIP archive: " + NomadString(message ? message : "unknown archive error"));
    }
}

la_ssize_t writeArchiveData(archive* writer, void* context, const void* data, const size_t size) {
    auto& output = *static_cast<std::ofstream*>(context);
    output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!output) {
        archive_set_error(writer, EIO, "Failed to write archive output");
        return -1;
    }
    return static_cast<la_ssize_t>(size);
}

void writeArchive(const NomadPath& destination, const NomadPath& staging, const std::vector<NomadPath>& files) {
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw ProjectPackagingError("Failed to open ZIP archive '" + pathToUtf8(destination) + "'");
    }
    const std::unique_ptr<archive, decltype(&archive_write_free)> writer(archive_write_new(), &archive_write_free);
    if (!writer) {
        throw ProjectPackagingError("Failed to allocate ZIP writer");
    }
    checkArchive(archive_write_set_format_zip(writer.get()), writer.get());
    checkArchive(archive_write_set_options(writer.get(), "zip:compression=deflate,zip:compression-level=6,zip:hdrcharset=UTF-8"), writer.get());
    checkArchive(archive_write_set_bytes_per_block(writer.get(), 0), writer.get());
    checkArchive(archive_write_open(writer.get(), &output, nullptr, &writeArchiveData, nullptr), writer.get());

    std::array<char, 65536> buffer{};
    for (const auto& file : files) {
        std::ifstream input(file, std::ios::binary);
        if (!input) {
            throw ProjectPackagingError("Failed to read staged file '" + pathToUtf8(file) + "'");
        }
        const std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), &archive_entry_free);
        if (!entry) {
            throw ProjectPackagingError("Failed to allocate ZIP entry");
        }
        const auto name = pathToGenericUtf8(file.lexically_relative(staging));
        archive_entry_set_pathname_utf8(entry.get(), name.c_str());
        archive_entry_set_filetype(entry.get(), AE_IFREG);
        const auto permissions = std::filesystem::status(file).permissions();
        const auto executable = (permissions & (std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_exec | std::filesystem::perms::others_exec)) != std::filesystem::perms::none;
        archive_entry_set_perm(entry.get(), executable ? 0755 : 0644);
        archive_entry_set_size(entry.get(), static_cast<la_int64_t>(std::filesystem::file_size(file)));
        archive_entry_set_mtime(entry.get(), 315532800, 0);
        checkArchive(archive_write_header(writer.get(), entry.get()), writer.get());
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto size = input.gcount();
            if (size > 0 && archive_write_data(writer.get(), buffer.data(), static_cast<size_t>(size)) != size) {
                throw ProjectPackagingError("Failed to write ZIP entry '" + name + "'");
            }
        }
        if (!input.eof()) {
            throw ProjectPackagingError("Failed to read staged file '" + pathToUtf8(file) + "'");
        }
        checkArchive(archive_write_finish_entry(writer.get()), writer.get());
    }
    checkArchive(archive_write_close(writer.get()), writer.get());
    output.close();
    if (!output) {
        throw ProjectPackagingError("Failed to close ZIP archive '" + pathToUtf8(destination) + "'");
    }
}

} // namespace

NomadStringView getPackagePlatform() {
    return NOMAD_PACKAGE_PLATFORM;
}

ProjectPackageResult packageProject(
    const ProjectConfiguration& configuration,
    const NomadPath& runtimeDirectory,
    const bool force,
    const bool dryRun
) {
    if (configuration.type != ProjectConfigurationType::Development) {
        throw ProjectPackagingError("A release manifest cannot be used to create another package");
    }

    if (!isProjectRelativeDirectory(configuration.package.output)) {
        throw ProjectPackagingError("Configuration field 'package.output' must be a project-relative directory");
    }

    const auto outputDirectory = (configuration.root / configuration.package.output).lexically_normal();
    const auto resources = resolveProjectResourcePath(configuration);

    if (
        outputDirectory == configuration.root ||
        isWithin(outputDirectory, resources) ||
        isWithin(resources, outputDirectory)
    ) {
        throw ProjectPackagingError("Package output must not contain or replace the project resources");
    }

    const auto validComponent = [](const NomadString& value) {
        return !value.empty() && value.find_first_of("/\\<>:\"|?*") == NomadString::npos &&
            std::ranges::none_of(value, [](const unsigned char character) { return character < 32; });
    };
    if (!validComponent(configuration.project.executable) || !validComponent(configuration.project.version)) {
        throw ProjectPackagingError("Project executable and version must be valid archive filename components");
    }
    const auto bundle = loadRuntimeBundle(configuration, runtimeDirectory);
    const auto output = outputDirectory / pathFromUtf8(configuration.project.executable + "-" +
        bundle.target + "-" + configuration.project.version + ".zip");
    auto staging = output;
    staging += ".staging";
    auto temporaryArchive = output;
    temporaryArchive += ".tmp";
    auto backup = output;
    backup += ".backup";
    std::error_code error;

    if (std::filesystem::exists(staging, error)) {
        throw ProjectPackagingError("Package staging directory already exists: '" + pathToUtf8(staging) + "'");
    }

    if (error) {
        raiseFilesystemError("Failed to inspect package staging directory", staging, error);
    }

    if (std::filesystem::exists(backup, error)) {
        throw ProjectPackagingError("Package backup directory already exists: '" + pathToUtf8(backup) + "'");
    }

    if (error) {
        raiseFilesystemError("Failed to inspect package backup directory", backup, error);
    }

    const auto outputExists = std::filesystem::exists(output, error);

    if (error) {
        raiseFilesystemError("Failed to inspect package output directory", output, error);
    }

    if (std::filesystem::exists(temporaryArchive, error)) {
        throw ProjectPackagingError("Temporary ZIP archive already exists: '" + pathToUtf8(temporaryArchive) + "'");
    }
    if (error) {
        raiseFilesystemError("Failed to inspect temporary ZIP archive", temporaryArchive, error);
    }
    if (outputExists && !std::filesystem::is_regular_file(output, error)) {
        throw ProjectPackagingError("Package output is not a regular file: '" + pathToUtf8(output) + "'");
    }

    if (error) {
        raiseFilesystemError("Failed to inspect package output directory", output, error);
    }

    if (outputExists && !force) {
        throw ProjectPackagingError(
            "Package archive already exists; use --force to replace it: '" + pathToUtf8(output) + "'"
        );
    }

    std::vector<NomadPath> stagedFiles;

    try {
        if (!dryRun) {
            createDirectories(staging);
        }

        auto resourceFiles = copyResources(configuration, staging, dryRun);
        stagedFiles.insert(stagedFiles.end(), resourceFiles.begin(), resourceFiles.end());
        auto runtimeFiles = copyRuntime(configuration, runtimeDirectory, bundle, staging, dryRun);
        stagedFiles.insert(stagedFiles.end(), runtimeFiles.begin(), runtimeFiles.end());
        const auto manifest = staging / NOMAD_PROJECT_FILE_NAME;

        if (!dryRun) {
            writeReleaseManifest(configuration, manifest);
        }

        stagedFiles.push_back(manifest);
        std::ranges::sort(stagedFiles, {}, [&](const auto& file) {
            return pathToGenericUtf8(file.lexically_relative(staging));
        });

        if (!dryRun) {
            writeArchive(temporaryArchive, staging, stagedFiles);
            std::filesystem::remove_all(staging, error);
            if (error) {
                raiseFilesystemError("Failed to remove package staging directory", staging, error);
            }
        }

        if (!dryRun && outputExists) {
            std::filesystem::rename(output, backup, error);

            if (error) {
                raiseFilesystemError("Failed to preserve existing package output", output, error);
            }
        }

        if (!dryRun) {
            std::filesystem::rename(temporaryArchive, output, error);

            if (error) {
                if (outputExists) {
                    std::error_code restoreError;
                    std::filesystem::rename(backup, output, restoreError);
                    if (restoreError) {
                        raiseFilesystemError("Failed to restore previous package; backup retained at", backup, restoreError);
                    }
                }

                raiseFilesystemError("Failed to publish package output", output, error);
            }

            if (outputExists) {
                std::filesystem::remove_all(backup, error);

                if (error) {
                    raiseFilesystemError("Failed to remove replaced package output", backup, error);
                }
            }
        }
    } catch (...) {
        if (!dryRun) {
            std::filesystem::remove_all(staging, error);
            if (error) {
                raiseFilesystemError("Failed to clean package staging directory", staging, error);
            }
            std::filesystem::remove(temporaryArchive, error);
            if (error) {
                raiseFilesystemError("Failed to clean temporary ZIP archive", temporaryArchive, error);
            }
        }

        throw;
    }

    std::vector<NomadPath> files;
    files.reserve(stagedFiles.size());

    for (const auto& stagedFile : stagedFiles) {
        files.push_back(stagedFile.lexically_relative(staging));
    }

    std::ranges::sort(files);
    return ProjectPackageResult{output, std::move(files)};
}

} // namespace nomad
