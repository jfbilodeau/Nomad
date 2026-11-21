// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptCallable.hpp>

#include <utility>

namespace nomad {

ScriptMethod::ScriptMethod(
    const CallableId id,
    NomadString name,
    const Type* classType,
    std::vector<Parameter> parameters,
    const Type* returnType,
    NomadString path,
    NomadString source
):
    ScriptCallable(
        id, 
        std::move(path), 
        std::move(source), 
        std::move(name), 
        classType,
        std::move(parameters), 
        returnType, 
        NomadString{}
    )
{
}

const ScriptMethod* ScriptMethod::asScriptMethod() const {
    return this;
}

} // namespace nomad
