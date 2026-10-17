// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <functional>
#include <vector>

namespace nomad {

using NativeFunctionFn = std::function<void(VirtualMachine*)>;

struct FunctionParameterDefinition {
    NomadString name;
    const Type* type;
    NomadDocField;
};

struct NativeFunctionDefinition {
    NomadId id = NOMAD_INVALID_ID;
    NomadString name;
    NativeFunctionFn fn;
    std::vector<FunctionParameterDefinition> parameters;
    const Type* returnType = nullptr;
    NomadDocField;
    const Type* objectType = nullptr;
    bool mutatesReceiver = false;
};


FunctionParameterDefinition defParameter(const NomadString& name, const Type* type, NomadDocArg);

using NativeFunctionFn = std::function<void(VirtualMachine*)>;

}
