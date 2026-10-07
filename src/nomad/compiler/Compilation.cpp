// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/script/Function.hpp>
#include <nomad/script/Documentation.hpp>
#include <nomad/script/Runtime.hpp>

#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>

namespace nomad {

namespace {

struct CompilationArtifacts {
    std::unique_ptr<Runtime> runtime;
    CompilationResult result;
};

CompilationArtifacts compilePath(const std::filesystem::path& path) {
    auto runtime = std::make_unique<Runtime>();
    auto compiler = runtime->createCompiler();
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

    return CompilationArtifacts{
        std::move(runtime),
        CompilationResult{
            {diagnostics.begin(), diagnostics.end()},
            context.getErrorCount(),
            context.getWarningCount()
        }
    };
}

} // namespace

bool CompilationResult::succeeded() const {
    return errorCount == 0;
}

CompilationResult checkPath(const std::filesystem::path& path) {
    return compilePath(path).result;
}

InstructionDumpResult dumpInstructions(
    const std::filesystem::path& path,
    const std::optional<NomadString>& functionName
) {
    auto artifacts = compilePath(path);
    InstructionDumpResult result{std::move(artifacts.result), {}};

    if (!result.compilation.succeeded()) {
        return result;
    }

    if (functionName) {
        const auto functionId = artifacts.runtime->getFunctionId(*functionName);
        const auto* function = artifacts.runtime->getFunction(functionId);

        if (function == nullptr) {
            result.compilation.diagnostics.push_back(Diagnostic{
                DiagnosticSeverity::Error,
                "Unknown function '" + *functionName + "'",
                path.generic_string(),
                NOMAD_INVALID_INDEX,
                NOMAD_INVALID_INDEX
            });
            ++result.compilation.errorCount;

            return result;
        }

        std::ostringstream output;
        artifacts.runtime->dumpInstructions(output, function);
        result.instructions = output.str();
        return result;
    }

    std::ostringstream output;
    artifacts.runtime->dumpInstructions(output);
    result.instructions = output.str();

    return result;
}

DocumentationResult generateDocumentationForPath(const std::filesystem::path& path) {
    auto artifacts = compilePath(path);
    DocumentationResult result{std::move(artifacts.result), {}};

    if (!result.compilation.succeeded()) {
        return result;
    }

    std::ostringstream output;
    generateDocumentation(artifacts.runtime.get(), output);
    result.documentation = output.str();

    return result;
}

} // namespace nomad
