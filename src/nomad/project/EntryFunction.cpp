// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/EntryFunction.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/Type.hpp>

#include <nomad/system/Path.hpp>

#include <utility>

namespace nomad {

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

} // namespace nomad
