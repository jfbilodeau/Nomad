// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerTools.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/script/Runtime.hpp>

#include <memory>

namespace nomad {

namespace {

CompilationResult compileWithRuntime(const SourceBatch& batch, Runtime* runtime, const CompilationTarget target) {
    std::unique_ptr<Runtime> ownedRuntime;
    if (runtime == nullptr) {
        ownedRuntime = std::make_unique<Runtime>();
        runtime = ownedRuntime.get();
    }
    auto compiler = runtime->createCompiler();
    return compileSources(batch, *compiler, target);
}

} // namespace

CompilationResult checkPath(const std::filesystem::path& path, Runtime* runtime) {
    return checkPaths({path}, runtime);
}

CompilationResult checkPaths(const std::vector<std::filesystem::path>& paths, Runtime* runtime) {
    return checkSources(loadSourcePaths(paths), runtime);
}

CompilationResult checkSources(const SourceBatch& batch, Runtime* runtime) {
    return compileWithRuntime(batch, runtime, CompilationTarget::Executable);
}

CompilationResult analyzeSources(const SourceBatch& batch, Runtime* runtime) {
    return compileWithRuntime(batch, runtime, CompilationTarget::Analysis);
}

} // namespace nomad
