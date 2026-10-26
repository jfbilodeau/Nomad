// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptMethod.hpp>

#include <utility>

namespace nomad {

ScriptMethod::ScriptMethod(
    NomadString name,
    const Type* objectType,
    std::vector<Parameter> parameters,
    const Type* returnType,
    NomadString path,
    NomadString source
):
    ScriptCallable(
        CallableId{}, std::move(path), std::move(source), std::move(name), objectType,
        std::move(parameters), returnType, NomadString{}
    )
{
}

const ScriptMethod* ScriptMethod::asScriptMethod() const {
    return this;
}

} // namespace nomad
