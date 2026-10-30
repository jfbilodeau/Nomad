// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Source.hpp>

namespace nomad {

bool CompilationResult::succeeded() const {
    return errorCount == 0;
}

CompilationResult compileSources(const SourceBatch& batch, Compiler& compiler, const CompilationTarget target) {
    CompilerContext context(&compiler);
    compiler.registerSources(&context, batch);
    (void)compiler.compileScripts(&context, target);
    const auto diagnostics = context.getDiagnostics();
    return {{diagnostics.begin(), diagnostics.end()}, context.getErrorCount(), context.getWarningCount()};
}

} // namespace nomad
