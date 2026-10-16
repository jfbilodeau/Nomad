// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerTools.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptFunction.hpp>
#include <nomad/script/Type.hpp>

#include <nomad/system/Path.hpp>

#include <memory>
#include <sstream>

namespace nomad {

namespace {

struct CompilationArtifacts {
    std::unique_ptr<Runtime> ownedRuntime;
    Runtime* runtime = nullptr;
    CompilationResult result;
};

// When `externalRuntime` is null a bare runtime is created, exposing only the language built-ins.
// Tools that need the engine API pass the runtime of a headless `Game`.
CompilationArtifacts compileSources(
    const SourceBatch& batch,
    Runtime* externalRuntime
) {
    std::unique_ptr<Runtime> ownedRuntime;

    if (externalRuntime == nullptr) {
        ownedRuntime = std::make_unique<Runtime>();
        externalRuntime = ownedRuntime.get();
    }

    auto* runtime = externalRuntime;
    auto compiler = runtime->createCompiler();
    CompilerContext context(compiler.get());

    compiler->registerScripts(&context, batch);
    (void)compiler->compileFunctions(&context);

    const auto diagnostics = context.getDiagnostics();

    return CompilationArtifacts{
        std::move(ownedRuntime),
        runtime,
        CompilationResult{
            {diagnostics.begin(), diagnostics.end()},
            context.getErrorCount(),
            context.getWarningCount()
        }
    };
}

CompilationArtifacts compilePaths(
    const std::vector<std::filesystem::path>& paths,
    Runtime* runtime
) {
    return compileSources(loadSourcePaths(paths), runtime);
}

} // namespace

bool CompilationResult::succeeded() const {
    return errorCount == 0;
}

CompilationResult checkPath(const std::filesystem::path& path, Runtime* runtime) {
    return checkPaths({path}, runtime);
}

CompilationResult checkPaths(const std::vector<std::filesystem::path>& paths, Runtime* runtime) {
    return compilePaths(paths, runtime).result;
}

CompilationResult checkSources(const SourceBatch& batch, Runtime* runtime) {
    return compileSources(batch, runtime).result;
}

void validateEntryFunction(
    CompilationResult& result,
    const Runtime* runtime,
    const NomadString& entryFunction,
    const std::filesystem::path& projectFile
) {
    if (!result.succeeded()) {
        return;
    }

    const auto functionId = runtime->getFunctionId(entryFunction);
    const auto* function = runtime->getFunction(functionId);
    NomadString message;

    if (function == nullptr) {
        message = "Configured entry function '" + entryFunction + "' was not found";
    } else if (function->getParameterCount() != 0) {
        message = "Configured entry function '" + entryFunction + "' must not have parameters";
    } else if (const auto* returnType = function->getReturnType(); returnType == nullptr || !returnType->isVoid()) {
        message = "Configured entry function '" + entryFunction + "' must return void";
    } else {
        return;
    }

    result.diagnostics.push_back(Diagnostic{
        DiagnosticSeverity::Error,
        std::move(message),
        pathToGenericUtf8(projectFile),
        NOMAD_INVALID_INDEX,
        NOMAD_INVALID_INDEX
    });
    ++result.errorCount;
}

InstructionDumpResult dumpInstructions(
    const std::filesystem::path& path,
    const std::optional<NomadString>& functionName,
    Runtime* runtime
) {
    return dumpInstructions(std::vector<std::filesystem::path>{path}, functionName, runtime);
}

InstructionDumpResult dumpInstructions(
    const std::vector<std::filesystem::path>& paths,
    const std::optional<NomadString>& functionName,
    Runtime* runtime
) {
    auto artifacts = compilePaths(paths, runtime);
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
                paths.empty() ? NomadString{} : pathToGenericUtf8(paths.front()),
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

DocumentationResult generateDocumentationForPath(const std::filesystem::path& path, Runtime* runtime) {
    return generateDocumentationForPaths({path}, runtime);
}

DocumentationResult generateDocumentationForPaths(
    const std::vector<std::filesystem::path>& paths,
    Runtime* runtime
) {
    auto artifacts = compilePaths(paths, runtime);
    DocumentationResult result{std::move(artifacts.result), {}};

    if (!result.compilation.succeeded()) {
        return result;
    }

    std::ostringstream output;
    generateDocumentation(artifacts.runtime, output);
    result.documentation = output.str();

    return result;
}

} // namespace nomad
