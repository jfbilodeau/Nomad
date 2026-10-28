// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/system/Path.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace nomad {

namespace {

struct SourceOverride {
    const ScriptSource* script;
    std::optional<std::filesystem::path> normalizedPath;
    bool used = false;
};

std::filesystem::path normalizeSourcePath(const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::filesystem::filesystem_error("Source backing path is empty", path,
            std::make_error_code(std::errc::invalid_argument));
    }
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    return (std::filesystem::weakly_canonical(absolute.parent_path()) / absolute.filename()).make_preferred();
}

bool sameSourcePath(const std::filesystem::path& left, const std::filesystem::path& right) {
#ifdef _WIN32
    const auto result = CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE);
    if (result == 0) {
        throw std::filesystem::filesystem_error("Failed to compare source paths", left, right,
            std::error_code(static_cast<int>(GetLastError()), std::system_category()));
    }
    return result == CSTR_EQUAL;
#else
    return left == right;
#endif
}

void reportOverrideError(SourceBatch& batch, const ScriptSource& script, NomadString message) {
    batch.diagnostics.push_back({DiagnosticSeverity::Error, std::move(message),
        script.source.sourceName, NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX});
}

void appendSource(SourceBatch& batch, ScriptSource script) {
    for (auto& existing : batch.sources) {
        if (script.source.filePath && existing.source.filePath &&
            sameSourcePath(normalizeSourcePath(*script.source.filePath),
                normalizeSourcePath(*existing.source.filePath))) {
            if (existing.owner == script.owner || script.owner.empty()) {
                return;
            }
            if (existing.owner.empty()) {
                existing = std::move(script);
                return;
            }
            reportOverrideError(batch, script, "Source file is owned by both modules '" +
                existing.owner + "' and '" + script.owner + "'");
            return;
        }
        if (existing.scriptName == script.scriptName &&
            (!existing.owner.empty() || !script.owner.empty())) {
            reportOverrideError(batch, script, "Source identity '" + script.scriptName +
                "' is already used by '" + existing.source.sourceName + "'");
            return;
        }
    }
    batch.sources.push_back(std::move(script));
}

ScriptSource describeSource(const SourceRoot& root, const NomadPath& relativePath) {
    auto scriptName = pathToGenericUtf8(relativePath.parent_path() / relativePath.stem());
    std::ranges::replace(scriptName, '/', '.');
    std::ranges::replace(scriptName, '\\', '.');
    if (!root.owner.empty()) {
        scriptName = "$" + root.owner + "." + scriptName;
    }
    return {std::move(scriptName), {}, root.owner, relativePath};
}

std::vector<SourceOverride> prepareOverrides(SourceBatch& batch, std::span<const ScriptSource> scripts) {
    std::vector<SourceOverride> overrides;
    for (const auto& script : scripts) {
        try {
            SourceOverride candidate{&script, std::nullopt};
            if (script.source.filePath) {
                candidate.normalizedPath = normalizeSourcePath(*script.source.filePath);
            }
            const auto duplicate = std::find_if(overrides.begin(), overrides.end(), [&](const auto& existing) {
                if (candidate.normalizedPath && existing.normalizedPath) {
                    return sameSourcePath(*candidate.normalizedPath, *existing.normalizedPath);
                }
                return !candidate.normalizedPath && !existing.normalizedPath &&
                    script.source.sourceName == existing.script->source.sourceName;
            });
            if (duplicate != overrides.end()) {
                reportOverrideError(batch, script,
                    "Duplicate source override for '" + duplicate->script->source.sourceName + "'");
            } else {
                overrides.push_back(std::move(candidate));
            }
        } catch (const std::filesystem::filesystem_error& exception) {
            reportOverrideError(batch, script, exception.what());
        }
    }
    return overrides;
}

bool applyOverride(SourceBatch& batch, const std::filesystem::path& path, ScriptSource script,
    std::vector<SourceOverride>& overrides) {
    if (overrides.empty()) {
        return false;
    }
    const auto normalizedPath = normalizeSourcePath(path);
    for (auto& replacement : overrides) {
        if (replacement.normalizedPath && sameSourcePath(normalizedPath, *replacement.normalizedPath)) {
            script.source = replacement.script->source;
            appendSource(batch, std::move(script));
            replacement.used = true;
            return true;
        }
    }
    return false;
}

void reportLoadError(SourceBatch& batch, const std::filesystem::path& path, NomadString message) {
    batch.diagnostics.push_back({
        DiagnosticSeverity::Error,
        std::move(message),
        pathToGenericUtf8(path),
        NOMAD_INVALID_INDEX,
        NOMAD_INVALID_INDEX
    });
}

void readSource(SourceBatch& batch, const std::filesystem::path& path, ScriptSource script,
    std::vector<SourceOverride>& overrides) {
    if (applyOverride(batch, path, script, overrides)) {
        return;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        reportLoadError(batch, path, "Failed to open source file");
        return;
    }

    NomadString text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) {
        reportLoadError(batch, path, "Failed to read source file");
        return;
    }

    script.source = {pathToGenericUtf8(path), std::move(text), path};
    appendSource(batch, std::move(script));
}

void scanDirectory(SourceBatch& batch, const SourceRoot& root,
    const std::filesystem::path& relative, NomadIndex remainingDepth, std::vector<SourceOverride>& overrides) {
    const auto path = root.path / relative;
    if (remainingDepth == 0) {
        log::debug("Skipping directory '" + pathToGenericUtf8(path) + "' (max depth reached)");
        return;
    }

    std::error_code error;
    auto iterator = std::filesystem::directory_iterator(path, error);
    if (error) {
        reportLoadError(batch, path, "Failed to scan directory: " + error.message());
        return;
    }

    const auto end = std::filesystem::directory_iterator();
    std::vector<NomadPath> entries;
    while (iterator != end) {
        entries.push_back(iterator->path());
        iterator.increment(error);
        if (error) {
            reportLoadError(batch, path, "Failed to continue scanning directory: " + error.message());
            break;
        }
    }
    std::sort(entries.begin(), entries.end());
    for (const auto& entry : entries) {
        const auto status = std::filesystem::symlink_status(entry, error);
        if (error) {
            reportLoadError(batch, entry, "Failed to inspect path: " + error.message());
        } else if (std::filesystem::is_symlink(status)) {
            log::debug("Skipping symbolic link '" + pathToGenericUtf8(entry) + "'");
        } else if (std::filesystem::is_directory(status)) {
            scanDirectory(batch, root, relative / entry.filename(), remainingDepth - 1, overrides);
        } else if (std::filesystem::is_regular_file(status) && entry.extension() == ".nomad") {
            readSource(batch, entry, describeSource(root, relative / entry.filename()), overrides);
        }
    }
}

} // namespace

std::vector<NomadPath> getScriptPaths(const NomadConfiguration& configuration) {
    configuration.validateSection("scripts");
    const auto directories = configuration.getStringArray("scripts.directories");
    if (!directories) {
        return {"scripts"};
    }
    try {
        return normalizeRelativeDirectories(*directories);
    } catch (const NomadException& error) {
        throw NomadConfigurationException(
            "Configuration field 'scripts.directories' (relative to the project root) " + NomadString(error.what())
        );
    }
}

std::vector<NomadPath> resolveScriptPaths (
    const NomadConfiguration& configuration,
    const NomadPath& projectDirectory
) {
    auto paths = getScriptPaths(configuration);
    for (auto& path : paths) {
        path = (projectDirectory / path).lexically_normal();
    }
    return paths;
}

std::vector<SourceRoot> getModuleScriptRoots(const std::span<const ModuleManifest> modules) {
    std::vector<SourceRoot> roots;
    for (const auto& module : modules) {
        if (module.scriptsDirectory) {
            roots.push_back({module.directory / *module.scriptsDirectory, module.name, module.scriptsDirectoryRequired});
        }
    }
    return roots;
}

std::optional<NomadPath> getSourceRelativePath(const NomadPath& root, const NomadPath& path) {
    const auto normalizedPath = normalizeSourcePath(path);
    if (sameSourcePath(normalizeSourcePath(root), normalizedPath)) {
        return normalizedPath.filename();
    }
    const auto normalizedRoot = std::filesystem::weakly_canonical(
        std::filesystem::absolute(root).lexically_normal()).make_preferred();
    auto component = normalizedPath.begin();
    for (const auto& rootComponent : normalizedRoot) {
        if (component == normalizedPath.end() || !sameSourcePath(rootComponent, *component)) {
            return std::nullopt;
        }
        ++component;
    }
    if (component == normalizedPath.end()) {
        return normalizedPath.filename();
    }
    NomadPath relative;
    for (; component != normalizedPath.end(); ++component) {
        relative /= *component;
    }
    return relative;
}

SourceBatch loadSourceRoots(std::span<const SourceRoot> roots, std::span<const ScriptSource> overrideSources) {
    SourceBatch batch;
    auto overrides = prepareOverrides(batch, overrideSources);
    auto orderedRoots = std::vector<SourceRoot>(roots.begin(), roots.end());
    std::stable_sort(orderedRoots.begin(), orderedRoots.end(), [](const auto& left, const auto& right) {
        return !left.owner.empty() && right.owner.empty();
    });
    for (const auto& root : orderedRoots) {
        const auto& path = root.path;
        try {
            if (root.owner.empty() && path.extension() == ".nomad" &&
                applyOverride(batch, path, describeSource(root, path.filename()), overrides)) {
                continue;
            }
            if (!std::filesystem::exists(path)) {
                if (root.required) {
                    reportLoadError(batch, path, "Path does not exist" +
                        (root.owner.empty() ? NomadString{} : " for module '" + root.owner + "'"));
                }
            } else if (std::filesystem::is_regular_file(path)) {
                if (!root.owner.empty()) {
                    reportLoadError(batch, path, "Expected a script directory for module '" + root.owner + "'");
                } else if (path.extension() != ".nomad") {
                    reportLoadError(batch, path, "Expected a .nomad source file");
                } else {
                    readSource(batch, path, describeSource(root, path.filename()), overrides);
                }
            } else if (std::filesystem::is_directory(path)) {
                scanDirectory(batch, root, {}, 10, overrides);
            } else {
                reportLoadError(batch, path, "Path is not a regular file or directory");
            }
        } catch (const std::filesystem::filesystem_error& exception) {
            reportLoadError(batch, path, exception.what());
        }
    }

    for (const auto& replacement : overrides) {
        if (!replacement.used) {
            try {
                auto script = *replacement.script;
                if (script.source.filePath) {
                    for (const auto& root : orderedRoots) {
                        if (root.path.empty()) {
                            continue;
                        }
                        const auto relative = getSourceRelativePath(root.path, *script.source.filePath);
                        if (relative) {
                            if (!root.owner.empty()) {
                                auto owned = describeSource(root, *relative);
                                owned.source = script.source;
                                script = std::move(owned);
                            } else {
                                script.relativePath = *relative;
                            }
                            break;
                        }
                    }
                }
                appendSource(batch, std::move(script));
            } catch (const std::filesystem::filesystem_error& exception) {
                reportOverrideError(batch, *replacement.script, exception.what());
            }
        }
    }

    std::stable_sort(batch.sources.begin(), batch.sources.end(), [](const ScriptSource& left, const ScriptSource& right) {
        if (left.scriptName != right.scriptName) {
            return left.scriptName < right.scriptName;
        }
        return left.source.sourceName < right.source.sourceName;
    });
    return batch;
}

SourceBatch loadSourcePaths(std::span<const std::filesystem::path> paths, std::span<const ScriptSource> overrides) {
    std::vector<SourceRoot> roots;
    for (const auto& path : paths) {
        roots.push_back({path});
    }
    return loadSourceRoots(roots, overrides);
}

} // namespace nomad
