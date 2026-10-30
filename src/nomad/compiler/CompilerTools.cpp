// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerTools.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
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

InstructionDumpResult formatInstructionDump(
    CompilationResult compilation,
    Runtime& runtime,
    const std::optional<NomadString>& functionName,
    const std::vector<std::filesystem::path>& paths
);

// When `externalRuntime` is null a bare runtime is created, exposing only the language built-ins.
// Tools that need the engine API pass the runtime of a headless `Game`.
CompilationArtifacts compileArtifacts(
    const SourceBatch& batch,
    Runtime* externalRuntime,
    CompilationTarget target = CompilationTarget::Executable
) {
    std::unique_ptr<Runtime> ownedRuntime;

    if (externalRuntime == nullptr) {
        ownedRuntime = std::make_unique<Runtime>();
        externalRuntime = ownedRuntime.get();
    }

    auto* runtime = externalRuntime;
    auto compiler = runtime->createCompiler();
    return CompilationArtifacts{
        std::move(ownedRuntime),
        runtime,
        compileSources(batch, *compiler, target)
    };
}

CompilationArtifacts compilePaths(
    const std::vector<std::filesystem::path>& paths,
    Runtime* runtime
) {
    return compileArtifacts(loadSourcePaths(paths), runtime);
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
    return compileArtifacts(batch, runtime).result;
}

CompilationResult analyzeSources(const SourceBatch& batch, Runtime* runtime) {
    return compileArtifacts(batch, runtime, CompilationTarget::Analysis).result;
}

CompilationResult compileSources(const SourceBatch& batch, Compiler& compiler, const CompilationTarget target) {
    CompilerContext context(&compiler);
    compiler.registerSources(&context, batch);
    (void)compiler.compileScripts(&context, target);
    const auto diagnostics = context.getDiagnostics();
    return {{diagnostics.begin(), diagnostics.end()}, context.getErrorCount(), context.getWarningCount()};
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
    const auto* function = runtime->getScriptFunction(functionId);
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
    return formatInstructionDump(std::move(artifacts.result), *artifacts.runtime, functionName, paths);
}

InstructionDumpResult dumpInstructions(
    const std::vector<std::filesystem::path>& paths,
    const std::optional<NomadString>& functionName,
    Compiler& compiler
) {
    return formatInstructionDump(compileSources(loadSourcePaths(paths), compiler),
        *compiler.getRuntime(), functionName, paths);
}

namespace {

InstructionDumpResult formatInstructionDump(
    CompilationResult compilation,
    Runtime& runtime,
    const std::optional<NomadString>& functionName,
    const std::vector<std::filesystem::path>& paths
) {
    InstructionDumpResult result{std::move(compilation), {}};

    if (!result.compilation.succeeded()) {
        return result;
    }

    if (functionName) {
        const auto functionId = runtime.getFunctionId(*functionName);
        const auto* function = runtime.getScriptFunction(functionId);

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
        runtime.dumpInstructions(output, function);
        result.instructions = output.str();
        return result;
    }

    std::ostringstream output;
    runtime.dumpInstructions(output);
    result.instructions = output.str();

    return result;
}

} // namespace

DocumentationResult generateDocumentationForPath(const std::filesystem::path& path, Compiler& compiler) {
    return generateDocumentationForPaths({path}, compiler);
}

DocumentationResult generateDocumentationForPaths(
    const std::vector<std::filesystem::path>& paths,
    Compiler& compiler
) {
    DocumentationResult result{compileSources(loadSourcePaths(paths), compiler), {}};
    if (result.compilation.succeeded()) {
        std::ostringstream output;
        generateDocumentation(compiler.getRuntime(), output);
        result.documentation = output.str();
    }
    return result;
}

} // namespace nomad
