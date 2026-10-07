// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/script/Runtime.hpp>

#include <fstream>
#include <iterator>

namespace nomad {

bool CompilationResult::succeeded() const {
    return errorCount == 0;
}

CompilationResult checkPath(const std::filesystem::path& path) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    const auto sourceName = path.generic_string();

    try {
        if (!std::filesystem::exists(path)) {
            context.reportError("Path does not exist", sourceName, NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX);
        } else if (std::filesystem::is_regular_file(path)) {
            if (path.extension() != ".nomad") {
                context.reportError(
                    "Expected a .nomad source file",
                    sourceName,
                    NOMAD_INVALID_INDEX,
                    NOMAD_INVALID_INDEX
                );
            } else {
                std::ifstream sourceFile(path, std::ios::binary);

                if (!sourceFile.is_open()) {
                    context.reportError(
                        "Failed to open source file",
                        sourceName,
                        NOMAD_INVALID_INDEX,
                        NOMAD_INVALID_INDEX
                    );
                } else {
                    const NomadString source{
                        std::istreambuf_iterator<char>(sourceFile),
                        std::istreambuf_iterator<char>()
                    };
                    (void)compiler->registerScriptFile(path.stem().string(), sourceName, source);
                }
            }
        } else if (std::filesystem::is_directory(path)) {
            compiler->loadScriptsFromPath(sourceName);
        } else {
            context.reportError(
                "Path is not a regular file or directory",
                sourceName,
                NOMAD_INVALID_INDEX,
                NOMAD_INVALID_INDEX
            );
        }
    } catch (const std::filesystem::filesystem_error& exception) {
        context.reportError(exception.what(), sourceName, NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX);
    }

    if (!context.hasError()) {
        (void)compiler->compileFunctions(&context);
    }

    const auto diagnostics = context.getDiagnostics();

    return CompilationResult{
        {diagnostics.begin(), diagnostics.end()},
        context.getErrorCount(),
        context.getWarningCount()
    };
}

} // namespace nomad
