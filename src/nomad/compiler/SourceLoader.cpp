// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/system/Path.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace nomad {

namespace {

void reportLoadError(SourceBatch& batch, const std::filesystem::path& path, NomadString message) {
    batch.diagnostics.push_back({
        DiagnosticSeverity::Error,
        std::move(message),
        pathToGenericUtf8(path),
        NOMAD_INVALID_INDEX,
        NOMAD_INVALID_INDEX
    });
}

void readSource(SourceBatch& batch, const std::filesystem::path& path, NomadString functionName) {
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

    batch.sources.push_back({std::move(functionName), {pathToGenericUtf8(path), std::move(text)}});
}

void scanDirectory(SourceBatch& batch, const std::filesystem::path& root,
    const std::filesystem::path& relative, NomadIndex remainingDepth) {
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
            scanDirectory(batch, root, relative / entry.path().filename(), remainingDepth - 1);
        } else if (std::filesystem::is_regular_file(status) && entry.path().extension() == ".nomad") {
            auto functionName = pathToGenericUtf8(relative / entry.path().stem());
            std::replace(functionName.begin(), functionName.end(), '/', '.');
            std::replace(functionName.begin(), functionName.end(), '\\', '.');
            readSource(batch, entry.path(), std::move(functionName));
        }

        iterator.increment(error);
        if (error) {
            reportLoadError(batch, path, "Failed to continue scanning directory: " + error.message());
            break;
        }
    }
}

} // namespace

SourceBatch loadSourcePaths(std::span<const std::filesystem::path> paths) {
    SourceBatch batch;
    for (const auto& path : paths) {
        try {
            if (!std::filesystem::exists(path)) {
                reportLoadError(batch, path, "Path does not exist");
            } else if (std::filesystem::is_regular_file(path)) {
                if (path.extension() != ".nomad") {
                    reportLoadError(batch, path, "Expected a .nomad source file");
                } else {
                    readSource(batch, path, pathToUtf8(path.stem()));
                }
            } else if (std::filesystem::is_directory(path)) {
                scanDirectory(batch, path, {}, 10);
            } else {
                reportLoadError(batch, path, "Path is not a regular file or directory");
            }
        } catch (const std::filesystem::filesystem_error& exception) {
            reportLoadError(batch, path, exception.what());
        }
    }

    std::stable_sort(batch.sources.begin(), batch.sources.end(), [](const ScriptSource& left, const ScriptSource& right) {
        if (left.functionName != right.functionName) {
            return left.functionName < right.functionName;
        }
        return left.source.sourceName < right.source.sourceName;
    });
    return batch;
}

} // namespace nomad
