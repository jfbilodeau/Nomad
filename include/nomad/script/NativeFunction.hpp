// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <functional>
#include <vector>

namespace nomad {

using NativeFunctionFn = std::function<void(VirtualMachine*)>;

struct NativeFunctionParameterDefinition {
    NomadString name;
    const Type* type;
    NomadDocField;
};

struct NativeFunctionDefinition {
    NomadId id;
    NomadString name;
    NativeFunctionFn fn;
    std::vector<NativeFunctionParameterDefinition> parameters;
    const Type* returnType;
    NomadDocField;
};


NativeFunctionParameterDefinition defParameter(const NomadString& name, const Type* type, NomadDocArg);

using NativeFunctionFn = std::function<void(VirtualMachine*)>;

}
