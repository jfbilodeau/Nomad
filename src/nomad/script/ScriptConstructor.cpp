// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptCallable.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <utility>

namespace nomad {

ScriptConstructor::ScriptConstructor(
    const CallableId id,
    const Type* classType,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
):
    ScriptCallable(
        id, 
        std::move(path), 
        std::move(source), 
        classType->getTypeName(),
        classType,
        std::move(parameters), 
        classType->getRuntime()->getVoidType(),
        NomadString{}
    )
{
}

const ScriptConstructor* ScriptConstructor::asScriptConstructor() const {
    return this;
}

const Type* ScriptConstructor::getConstructedType() const {
    return getClassType();
}

} // namespace nomad
