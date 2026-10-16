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
    return std::filesystem::absolute(path).lexically_normal().make_preferred();
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

bool applyOverride(SourceBatch& batch, const std::filesystem::path& path, const NomadString& scriptName,
    std::vector<SourceOverride>& overrides) {
    if (overrides.empty()) {
        return false;
    }
    const auto normalizedPath = normalizeSourcePath(path);
    for (auto& replacement : overrides) {
        if (replacement.normalizedPath && sameSourcePath(normalizedPath, *replacement.normalizedPath)) {
            batch.sources.push_back({scriptName, replacement.script->source});
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

void readSource(SourceBatch& batch, const std::filesystem::path& path, NomadString scriptName,
    std::vector<SourceOverride>& overrides) {
    if (applyOverride(batch, path, scriptName, overrides)) {
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

    batch.sources.push_back({std::move(scriptName), {pathToGenericUtf8(path), std::move(text), path}});
}

void scanDirectory(SourceBatch& batch, const std::filesystem::path& root,
    const std::filesystem::path& relative, NomadIndex remainingDepth, std::vector<SourceOverride>& overrides) {
    const auto path = root / relative;
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
    while (iterator != end) {
        const auto entry = *iterator;
        const auto status = entry.symlink_status(error);
        if (error) {
            reportLoadError(batch, entry.path(), "Failed to inspect path: " + error.message());
        } else if (std::filesystem::is_symlink(status)) {
            log::debug("Skipping symbolic link '" + pathToGenericUtf8(entry.path()) + "'");
        } else if (std::filesystem::is_directory(status)) {
            scanDirectory(batch, root, relative / entry.path().filename(), remainingDepth - 1, overrides);
        } else if (std::filesystem::is_regular_file(status) && entry.path().extension() == ".nomad") {
            auto scriptName = pathToGenericUtf8(relative / entry.path().stem());
            std::replace(scriptName.begin(), scriptName.end(), '/', '.');
            std::replace(scriptName.begin(), scriptName.end(), '\\', '.');
            readSource(batch, entry.path(), std::move(scriptName), overrides);
        }

        iterator.increment(error);
        if (error) {
            reportLoadError(batch, path, "Failed to continue scanning directory: " + error.message());
            break;
        }
    }
}

} // namespace

SourceBatch loadSourcePaths(std::span<const std::filesystem::path> paths, std::span<const ScriptSource> scripts) {
    SourceBatch batch;
    auto overrides = prepareOverrides(batch, scripts);
    for (const auto& path : paths) {
        try {
            if (path.extension() == ".nomad" && applyOverride(batch, path, pathToUtf8(path.stem()), overrides)) {
                continue;
            }
            if (!std::filesystem::exists(path)) {
                reportLoadError(batch, path, "Path does not exist");
            } else if (std::filesystem::is_regular_file(path)) {
                if (path.extension() != ".nomad") {
                    reportLoadError(batch, path, "Expected a .nomad source file");
                } else {
                    readSource(batch, path, pathToUtf8(path.stem()), overrides);
                }
            } else if (std::filesystem::is_directory(path)) {
                scanDirectory(batch, path, {}, 10, overrides);
            } else {
                reportLoadError(batch, path, "Path is not a regular file or directory");
            }
        } catch (const std::filesystem::filesystem_error& exception) {
            reportLoadError(batch, path, exception.what());
        }
    }

    for (const auto& replacement : overrides) {
        if (!replacement.used) {
            batch.sources.push_back(*replacement.script);
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

} // namespace nomad
