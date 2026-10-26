// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptConstructor.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <utility>

namespace nomad {

ScriptConstructor::ScriptConstructor(
    const Type* objectType,
    std::vector<Parameter> parameters,
    NomadString path,
    NomadString source
):
    ScriptCallable(
        CallableId{}, 
        std::move(path), 
        std::move(source), 
        objectType->getTypeName(), 
        objectType,
        std::move(parameters), 
        objectType->getRuntime()->getVoidType(), 
        NomadString{}
    )
{
    for (const auto& parameter : getParameters()) {
        registerVariable(parameter.name, parameter.type);
    }
}

const ScriptConstructor* ScriptConstructor::asScriptConstructor() const {
    return this;
}

const Type* ScriptConstructor::getConstructedType() const {
    return getObjectType();
}

} // namespace nomad
