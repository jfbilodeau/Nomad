// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectPackager.hpp>

#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/system/ModuleHost.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/system/ProjectSession.hpp>
#include <nomad/system/String.hpp>

#include <boost/json.hpp>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
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
    throw ProjectPackagingException(
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

void copyDirectory(
    const NomadPath& resources,
    const NomadPath& destination,
    const std::vector<NomadString>& exclusions,
    const std::set<NomadPath>& excludedResources,
    const std::function<void(const NomadPath&, const NomadPath&)>& copyResource
) {
    std::error_code error;

    if (!std::filesystem::is_directory(resources, error)) {
        if (error) {
            raiseFilesystemError("Failed to inspect resource directory", resources, error);
        }

        throw ProjectPackagingException("Resource directory does not exist: '" + pathToUtf8(resources) + "'");
    }

    std::filesystem::recursive_directory_iterator iterator(resources, error);
    const std::filesystem::recursive_directory_iterator end;

    while (iterator != end) {
        if (error) {
            raiseFilesystemError("Failed to read resource directory", resources, error);
        }

        const auto& entry = *iterator;

        if (entry.is_symlink(error)) {
            throw ProjectPackagingException(
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

            if (!isExcluded(relative, exclusions) &&
                !excludedResources.contains(relative.lexically_normal())) {
                copyResource(entry.path(), destination / relative);
            }
        } else if (!entry.is_directory(error)) {
            throw ProjectPackagingException("Unsupported project resource: '" + pathToUtf8(entry.path()) + "'");
        }

        if (error) {
            raiseFilesystemError("Failed to inspect resource", entry.path(), error);
        }

        iterator.increment(error);
    }
    if (error) {
        raiseFilesystemError("Failed to read resource directory", resources, error);
    }
}

struct RuntimeBundleFile {
    NomadPath path;
    NomadString role;
};

struct RuntimeBundle {
    NomadString target;
    std::vector<RuntimeBundleFile> files;
};

RuntimeBundle loadRuntimeBundle(
    const NomadProject& project,
    const NomadPath& directory,
    const bool module = false
) {
    const auto manifest = directory / "runtime.json";
    std::error_code manifestError;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(manifest, manifestError))) {
        throw ProjectPackagingException("Runtime manifest must not be a symbolic link");
    }
    std::ifstream input(manifest, std::ios::binary);
    if (!input) {
        throw ProjectPackagingException("Failed to open runtime manifest '" + pathToUtf8(manifest) + "'");
    }
    const NomadString text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) {
        throw ProjectPackagingException("Failed to read runtime manifest '" + pathToUtf8(manifest) + "'");
    }
    boost::system::error_code parseError;
    const auto value = boost::json::parse(text, parseError);
    if (parseError || !value.is_object()) {
        throw ProjectPackagingException("Invalid runtime manifest JSON: '" + pathToUtf8(manifest) + "'");
    }
    const auto& object = value.as_object();
    const auto* schema = object.if_contains("schema");
    const auto* version = object.if_contains("version");
    const auto* target = object.if_contains("target");
    const auto* entries = object.if_contains("files");
    if (!schema || !schema->is_int64() || schema->as_int64() != 1 ||
        (!module && (!version || !target)) ||
        (version && !version->is_string()) || (target && !target->is_string()) ||
        !entries || !entries->is_array()) {
        throw ProjectPackagingException("Runtime manifest requires schema 1 and files (host bundles also require version and target)");
    }
    if (version && version->as_string() != project.configuration.sdk.version.toString()) {
        throw ProjectPackagingException("Runtime bundle version does not match the project's required Nomad version");
    }
    RuntimeBundle bundle{target ? NomadString(target->as_string()) : NomadString{}, {}};
    if ((target && bundle.target.empty()) || std::ranges::any_of(bundle.target, [](const unsigned char character) {
        return !(character >= 'a' && character <= 'z') && !(character >= '0' && character <= '9') && character != '-';
    })) {
        throw ProjectPackagingException("Runtime manifest target must contain only lowercase letters, digits, and hyphens");
    }
    auto runtimeCount = 0;
    auto licenseCount = 0;
    std::set<NomadString> paths;
    for (const auto& entry : entries->as_array()) {
        if (!entry.is_object()) {
            throw ProjectPackagingException("Runtime manifest file entries must be objects");
        }
        const auto* pathValue = entry.as_object().if_contains("path");
        const auto* roleValue = entry.as_object().if_contains("role");
        if (!pathValue || !pathValue->is_string() || !roleValue || !roleValue->is_string()) {
            throw ProjectPackagingException("Runtime manifest files require string path and role fields");
        }
        const NomadString name(pathValue->as_string());
        const auto path = pathFromUtf8(name);
        if (name.empty() || name.find_first_of("\\:") != NomadString::npos ||
            std::ranges::any_of(name, [](const unsigned char character) { return character < 32; }) ||
            pathToGenericUtf8(path.lexically_normal()) != name ||
            path.is_absolute() || path.has_root_name() || std::ranges::any_of(path, [](const auto& part) {
                return part == ".." || part == ".";
            }) || !paths.insert(name).second) {
            throw ProjectPackagingException("Unsafe or duplicate runtime manifest path: '" + name + "'");
        }
        const NomadString role(roleValue->as_string());
        if (role == "runtime") {
            if (module) {
                throw ProjectPackagingException("Module runtime manifests must not declare a runtime executable");
            }
            ++runtimeCount;
            if (path.has_parent_path()) {
                throw ProjectPackagingException("Runtime executable must be at the bundle root");
            }
        } else if (role == "license") {
            ++licenseCount;
        } else if (role != "library") {
            throw ProjectPackagingException("Unknown runtime manifest file role: '" + role + "'");
        }
        auto current = directory;
        for (const auto& part : path) {
            current /= part;
            std::error_code error;
            const auto status = std::filesystem::symlink_status(current, error);
            if (error || std::filesystem::is_symlink(status)) {
                throw ProjectPackagingException("Missing or symbolic-linked runtime file: '" + name + "'");
            }
        }
        if (!std::filesystem::is_regular_file(directory / path)) {
            throw ProjectPackagingException("Declared runtime file is not a regular file: '" + name + "'");
        }
        bundle.files.push_back({path, role});
    }
    if (!module && (runtimeCount != 1 || licenseCount == 0)) {
        throw ProjectPackagingException("Runtime manifest must declare exactly one runtime executable and at least one license");
    }
    return bundle;
}

void validatePackagePath(const NomadPath& path) {
    const auto name = pathToGenericUtf8(path);
    if (name.empty() || path.is_absolute() || path.has_root_name() ||
        name.find_first_of("\\:<>\"|?*") != NomadString::npos ||
        std::ranges::any_of(name, [](const unsigned char character) { return character < 32; }) ||
        path.lexically_normal() != path || std::ranges::any_of(path, [](const auto& part) {
            return part == "." || part == ".." || part.empty();
        })) {
        throw ProjectPackagingException("Unsafe package entry: '" + name + "'");
    }
}

void validateSourceFile(const NomadPath& source) {
    const auto absolute = std::filesystem::absolute(source);
    auto current = absolute.root_path();
    for (const auto& part : absolute.relative_path()) {
        current /= part;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        if (error || std::filesystem::is_symlink(status)) {
            throw ProjectPackagingException("Missing or symbolic-linked package source: '" + pathToUtf8(source) + "'");
        }
    }
    if (!std::filesystem::is_regular_file(source)) {
        throw ProjectPackagingException("Package source is not a regular file: '" + pathToUtf8(source) + "'");
    }
}

bool haveIdenticalContent(const NomadPath& first, const NomadPath& second) {
    if (std::filesystem::file_size(first) != std::filesystem::file_size(second)) {
        return false;
    }
    std::ifstream firstInput(first, std::ios::binary);
    std::ifstream secondInput(second, std::ios::binary);
    if (!firstInput || !secondInput) {
        throw ProjectPackagingException("Failed to compare native runtime dependencies");
    }
    std::array<char, 65536> firstBuffer{};
    std::array<char, 65536> secondBuffer{};
    while (firstInput) {
        firstInput.read(firstBuffer.data(), static_cast<std::streamsize>(firstBuffer.size()));
        secondInput.read(secondBuffer.data(), static_cast<std::streamsize>(secondBuffer.size()));
        if (firstInput.gcount() != secondInput.gcount() ||
            !std::equal(firstBuffer.begin(), firstBuffer.begin() + firstInput.gcount(), secondBuffer.begin())) {
            return false;
        }
    }
    if (!firstInput.eof() || !secondInput.eof()) {
        throw ProjectPackagingException("Failed to compare native runtime dependencies");
    }
    return true;
}

class PackageFiles {
public:
    PackageFiles(NomadPath staging, const bool dryRun) :
        m_staging(std::move(staging)), m_dryRun(dryRun) {}

    void copy(const NomadPath& source, const NomadPath& relative, const bool nativeDependency = false) {
        validateSourceFile(source);
        validatePackagePath(relative);
        const auto key = stringToLowerAscii(pathToGenericUtf8(relative));
        const auto existing = m_entries.find(key);
        if (existing != m_entries.end() && existing->second.relative == relative &&
            !existing->second.source.empty() && std::filesystem::equivalent(existing->second.source, source)) {
            return;
        }
        if (existing != m_entries.end() && nativeDependency && existing->second.nativeDependency &&
            haveIdenticalContent(existing->second.source, source)) {
            return;
        }
        reserve(relative, source, nativeDependency);
        if (!m_dryRun) {
            copyFile(source, m_staging / relative);
        }
    }

    void write(const NomadPath& relative, const NomadStringView content) {
        reserve(relative);
        if (!m_dryRun) {
            const auto destination = m_staging / relative;
            createDirectories(destination.parent_path());
            std::ofstream output(destination, std::ios::binary | std::ios::trunc);
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.close();
            if (!output) {
                throw ProjectPackagingException("Failed to write package entry: '" + pathToUtf8(relative) + "'");
            }
        }
    }

    void createDirectory(const NomadPath& relative) {
        reserve(relative, {}, false, true);
        if (!m_dryRun) {
            createDirectories(m_staging / relative);
        }
    }

    void reserve(
        const NomadPath& relative,
        const NomadPath& source = {},
        const bool nativeDependency = false,
        const bool directory = false
    ) {
        validatePackagePath(relative);
        const auto key = stringToLowerAscii(pathToGenericUtf8(relative));
        const auto next = m_entries.lower_bound(key);
        if (next != m_entries.end() && next->first == key && directory && next->second.directory) {
            return;
        }
        if (next != m_entries.end() && (next->first == key ||
            (!directory && next->first.starts_with(key + "/")))) {
            throw ProjectPackagingException("Package entry collision: '" + pathToUtf8(relative) + "'");
        }
        for (auto parent = relative.parent_path(); !parent.empty(); parent = parent.parent_path()) {
            const auto entry = m_entries.find(stringToLowerAscii(pathToGenericUtf8(parent)));
            if (entry != m_entries.end() && !entry->second.directory) {
                throw ProjectPackagingException("Package entry collision: '" + pathToUtf8(relative) + "'");
            }
        }
        m_entries.emplace(key, Entry{relative, source, nativeDependency, directory});
    }

    [[nodiscard]] std::vector<NomadPath> getFiles() const {
        std::vector<NomadPath> files;
        for (const auto& [key, entry] : m_entries) {
            const auto file = m_staging / entry.relative;
            if (!m_dryRun && !entry.directory) {
                validateSourceFile(file);
            }
            files.push_back(file);
        }
        return files;
    }

private:
    struct Entry {
        NomadPath relative;
        NomadPath source;
        bool nativeDependency;
        bool directory;
    };
    NomadPath m_staging;
    bool m_dryRun;
    std::map<NomadString, Entry> m_entries;
};

void checkArchive(const int result, archive* writer) {
    if (result != ARCHIVE_OK) {
        const auto* message = archive_error_string(writer);
        throw ProjectPackagingException("Failed to write ZIP archive: " + NomadString(message ? message : "unknown archive error"));
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

} // namespace

void writeDeterministicZipArchive(
    const NomadPath& destination,
    const NomadPath& root,
    const std::vector<NomadPath>& files
) {
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw ProjectPackagingException("Failed to open ZIP archive '" + pathToUtf8(destination) + "'");
    }
    const std::unique_ptr<archive, decltype(&archive_write_free)> writer(archive_write_new(), &archive_write_free);
    if (!writer) {
        throw ProjectPackagingException("Failed to allocate ZIP writer");
    }
    checkArchive(archive_write_set_format_zip(writer.get()), writer.get());
    checkArchive(archive_write_set_options(writer.get(), "zip:compression=deflate,zip:compression-level=6,zip:hdrcharset=UTF-8"), writer.get());
    checkArchive(archive_write_set_bytes_per_block(writer.get(), 0), writer.get());
    checkArchive(archive_write_open(writer.get(), &output, nullptr, &writeArchiveData, nullptr), writer.get());

    std::array<char, 65536> buffer{};
    for (const auto& file : files) {
        const auto directory = std::filesystem::is_directory(file);
        const std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(), &archive_entry_free);
        if (!entry) {
            throw ProjectPackagingException("Failed to allocate ZIP entry");
        }
        const auto name = pathToGenericUtf8(file.lexically_relative(root)) + (directory ? "/" : "");
        archive_entry_set_pathname_utf8(entry.get(), name.c_str());
        archive_entry_set_filetype(entry.get(), directory ? AE_IFDIR : AE_IFREG);
        const auto permissions = std::filesystem::status(file).permissions();
        const auto executable = (permissions & (std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_exec | std::filesystem::perms::others_exec)) != std::filesystem::perms::none;
        archive_entry_set_perm(entry.get(), directory || executable ? 0755 : 0644);
        archive_entry_set_size(entry.get(), directory ? 0 : static_cast<la_int64_t>(std::filesystem::file_size(file)));
        archive_entry_set_mtime(entry.get(), 315532800, 0);
        checkArchive(archive_write_header(writer.get(), entry.get()), writer.get());
        if (directory) {
            checkArchive(archive_write_finish_entry(writer.get()), writer.get());
            continue;
        }
        std::ifstream input(file, std::ios::binary);
        if (!input) {
            throw ProjectPackagingException("Failed to read staged file '" + pathToUtf8(file) + "'");
        }
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto size = input.gcount();
            if (size > 0 && archive_write_data(writer.get(), buffer.data(), static_cast<size_t>(size)) != size) {
                throw ProjectPackagingException("Failed to write ZIP entry '" + name + "'");
            }
        }
        if (!input.eof()) {
            throw ProjectPackagingException("Failed to read staged file '" + pathToUtf8(file) + "'");
        }
        checkArchive(archive_write_finish_entry(writer.get()), writer.get());
    }
    checkArchive(archive_write_close(writer.get()), writer.get());
    output.close();
    if (!output) {
        throw ProjectPackagingException("Failed to close ZIP archive '" + pathToUtf8(destination) + "'");
    }
}

NomadStringView getPackagePlatform() {
    return NOMAD_PACKAGE_PLATFORM;
}

ProjectPackageResult packageProject(
    const NomadProject& project,
    const NomadPath& runtimeDirectory,
    const bool force,
    const bool dryRun
) {
    const auto& configuration = project.configuration;

    if (configuration.type != ProjectConfigurationType::Development) {
        throw ProjectPackagingException("A release manifest cannot be used to create another package");
    }

    if (!isProjectRelativeDirectory(configuration.package.output)) {
        throw ProjectPackagingException("Configuration field 'package.output' must be a project-relative directory");
    }

    const auto outputDirectory = (project.root / configuration.package.output).lexically_normal();
    if (outputDirectory == project.root) {
        throw ProjectPackagingException("Package output must not replace the project");
    }
    const auto scriptDirectories = getScriptPaths(project.document);
    for (const auto& directory : scriptDirectories) {
        const auto source = (project.root / directory).lexically_normal();
        if (isWithin(outputDirectory, source) || isWithin(source, outputDirectory)) {
            throw ProjectPackagingException("Package output must not contain or replace the project scripts");
        }
    }

    const auto validComponent = [](const NomadString& value) {
        return !value.empty() && value.find_first_of("/\\<>:\"|?*") == NomadString::npos &&
            std::ranges::none_of(value, [](const unsigned char character) { return character < 32; });
    };
    if (!validComponent(configuration.project.executable) || !validComponent(configuration.project.version)) {
        throw ProjectPackagingException("Project executable and version must be valid archive filename components");
    }
    const auto bundle = loadRuntimeBundle(project, runtimeDirectory);
    std::vector<ModuleManifest> modules;
    std::vector<RuntimeBundle> moduleBundles;
    std::unique_ptr<ModuleHost> moduleHost;
    try {
        modules = resolveProjectModules(project, runtimeDirectory.parent_path());
        for (const auto& module : modules) {
            auto dependencies = loadRuntimeBundle(project, module.directory, true);
            if (!dependencies.target.empty() && dependencies.target != bundle.target) {
                throw ProjectPackagingException("Module runtime bundle target does not match the host bundle");
            }
            moduleBundles.push_back(std::move(dependencies));
        }
        moduleHost = ModuleHost::create(
            project.document, project.root, modules, std::vector<NomadString>{}, ModuleMode::Packaging,
            project.configuration.project
        );
    } catch (const ModuleException& error) {
        throw ProjectPackagingException("Failed to load packaging modules: " + NomadString(error.what()));
    }
    const auto moduleScriptRoots = getModuleScriptRoots(modules);
    const auto moduleSources = loadSourceRoots(moduleScriptRoots);
    if (!moduleSources.diagnostics.empty()) {
        throw ProjectPackagingException("Failed to package module scripts: " +
            formatDiagnostic(moduleSources.diagnostics.front()));
    }
    std::set<std::pair<NomadString, NomadPath>> moduleSourcePaths;
    for (const auto& source : moduleSources.sources) {
        moduleSourcePaths.emplace(source.owner, source.relativePath);
    }
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
        throw ProjectPackagingException("Package staging directory already exists: '" + pathToUtf8(staging) + "'");
    }

    if (error) {
        raiseFilesystemError("Failed to inspect package staging directory", staging, error);
    }

    if (std::filesystem::exists(backup, error)) {
        throw ProjectPackagingException("Package backup directory already exists: '" + pathToUtf8(backup) + "'");
    }

    if (error) {
        raiseFilesystemError("Failed to inspect package backup directory", backup, error);
    }

    const auto outputExists = std::filesystem::exists(output, error);

    if (error) {
        raiseFilesystemError("Failed to inspect package output directory", output, error);
    }

    if (std::filesystem::exists(temporaryArchive, error)) {
        throw ProjectPackagingException("Temporary ZIP archive already exists: '" + pathToUtf8(temporaryArchive) + "'");
    }
    if (error) {
        raiseFilesystemError("Failed to inspect temporary ZIP archive", temporaryArchive, error);
    }
    if (outputExists && !std::filesystem::is_regular_file(output, error)) {
        throw ProjectPackagingException("Package output is not a regular file: '" + pathToUtf8(output) + "'");
    }

    if (error) {
        raiseFilesystemError("Failed to inspect package output directory", output, error);
    }

    if (outputExists && !force) {
        throw ProjectPackagingException(
            "Package archive already exists; use --force to replace it: '" + pathToUtf8(output) + "'"
        );
    }

    std::vector<NomadPath> stagedFiles;

    try {
        if (!dryRun) {
            createDirectories(staging);
        }

        PackageFiles packageFiles(staging, dryRun);
        const auto copyNativeBundle = [&](const RuntimeBundle& nativeBundle, const NomadPath& directory) {
            for (const auto& entry : nativeBundle.files) {
                auto relative = entry.path;
                if (entry.role == "runtime") {
                    relative = pathFromUtf8(configuration.project.executable);
                    relative += entry.path.extension();
                }
                if (stringToLowerAscii(pathToUtf8(*relative.begin())) == "res") {
                    throw ProjectPackagingException("Runtime file collides with package resources: '" + pathToUtf8(relative) + "'");
                }
                packageFiles.copy(directory / entry.path, relative, entry.role != "runtime");
            }
        };
        copyNativeBundle(bundle, runtimeDirectory);
        std::vector<NomadString> moduleNames;
        for (auto index = NomadIndex{0}; index < modules.size(); ++index) {
            const auto& module = modules[index];
            const auto library = getModuleLibraryName(module.name);
            const auto destination = NomadPath("modules") / pathFromUtf8(module.name);
            packageFiles.copy(module.directory / library, destination / library, true);
            packageFiles.copy(module.directory / "module.toml", destination / "module.toml");
            auto dependencies = moduleBundles[index];
            std::erase_if(dependencies.files, [&](const auto& entry) {
                return entry.path == library;
            });
            copyNativeBundle(dependencies, module.directory);
            if (std::filesystem::is_directory(module.directory / "scripts")) {
                packageFiles.createDirectory(destination / "scripts");
            }
            for (const auto& source : moduleSources.sources) {
                if (source.owner == module.name) {
                    packageFiles.copy(
                        *source.source.filePath, destination / "scripts" / source.relativePath
                    );
                }
            }
            moduleNames.push_back(module.name);
        }

        auto release = NomadConfiguration::parse(serializeReleaseProject(project));
        release.setStringArray("nomad.modules", moduleNames);
        std::set<NomadPath> excludedResources;
        ModulePackageContext context{};
        context.source = &project.document;
        context.release = &release;
        context.projectDirectory = project.root;
        context.stagingDirectory = staging;
        context.dryRun = dryRun;
        std::map<NomadPath, NomadPath> copiedResources;
        auto packagedScriptDirectories = scriptDirectories;
        context.copyFile = [&](const NomadPath& source, const NomadPath& relative) {
            if (source.extension() == ".nomad") {
                for (const auto& root : moduleScriptRoots) {
                    const auto moduleRelative = getSourceRelativePath(root.path, source);
                    if (moduleRelative && moduleSourcePaths.contains({root.owner, *moduleRelative})) {
                        return;
                    }
                }
            }
            const auto [entry, inserted] = copiedResources.emplace(relative.lexically_normal(), source.lexically_normal());
            if (inserted || entry->second != source.lexically_normal()) {
                packageFiles.copy(source, relative);
            }
        };
        context.copyDirectory = [&](const NomadPath& source, const NomadPath& destination) {
            if (isWithin(outputDirectory, source) || isWithin(source, outputDirectory)) {
                throw ProjectPackagingException("Package output must not contain or replace the project resources");
            }
            copyDirectory(source, destination, configuration.package.exclude, excludedResources, context.copyFile);
            for (auto index = NomadIndex{0}; index < scriptDirectories.size(); ++index) {
                const auto scriptPath = (project.root / scriptDirectories[index]).lexically_normal();
                const auto relative = scriptPath.lexically_relative(source.lexically_normal());
                if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..") {
                    packagedScriptDirectories[index] = (destination / relative).lexically_normal();
                }
            }
        };
        context.writeFile = [&](const NomadPath& relative, const NomadStringView content) {
            packageFiles.write(relative, content);
        };
        context.recordFile = [&](const NomadPath& relative) {
            packageFiles.reserve(relative);
        };
        context.excludeResource = [&](const NomadPath& relative) {
            validatePackagePath(relative);
            excludedResources.insert(relative.lexically_normal());
        };
        context.isResourceExcluded = [&](const NomadPath& relative) {
            return isExcluded(relative, configuration.package.exclude);
        };
        try {
            moduleHost->package(context);
        } catch (const std::exception& moduleError) {
            throw ProjectPackagingException("Module packaging failed: " + NomadString(moduleError.what()));
        }
        auto releaseScripts = getScriptPaths(release);
        if (releaseScripts.size() != scriptDirectories.size()) {
            throw ProjectPackagingException("Module packaging must preserve the number of script directories");
        }
        for (auto index = NomadIndex{0}; index < scriptDirectories.size(); ++index) {
            if (releaseScripts[index] == scriptDirectories[index]) {
                releaseScripts[index] = packagedScriptDirectories[index];
            }
            const auto source = project.root / scriptDirectories[index];
            for (const auto& module : modules) {
                const auto root = module.directory / "scripts";
                const auto relative = getSourceRelativePath(root, source);
                if (relative && std::filesystem::is_directory(root)) {
                    releaseScripts[index] = NomadPath("modules") / pathFromUtf8(module.name) / "scripts";
                    if (!std::filesystem::equivalent(root, source)) {
                        releaseScripts[index] /= *relative;
                    }
                    break;
                }
            }
        }
        std::vector<NomadString> releaseScriptPaths;
        for (const auto& path : releaseScripts) {
            releaseScriptPaths.push_back(pathToGenericUtf8(path));
        }
        release.setStringArray("scripts.directories", releaseScriptPaths);
        (void)getScriptPaths(release);
        for (auto index = NomadIndex{0}; index < scriptDirectories.size(); ++index) {
            context.copyDirectory(project.root / scriptDirectories[index], releaseScripts[index]);
        }
        release.setStringArray("nomad.modules", moduleNames);
        packageFiles.write(pathFromUtf8(NOMAD_PROJECT_FILE_NAME), release.serialize());
        stagedFiles = packageFiles.getFiles();
        std::ranges::sort(stagedFiles, {}, [&](const auto& file) {
            return pathToGenericUtf8(file.lexically_relative(staging));
        });

        if (!dryRun) {
            writeDeterministicZipArchive(temporaryArchive, staging, stagedFiles);
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
