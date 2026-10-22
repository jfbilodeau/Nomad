// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/Member.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <functional>
#include <vector>

namespace nomad {

class Type;
class ObjectType;
class VirtualMachine;

using NativePropertyGetFn = std::function<void(VirtualMachine*, RuntimeValue&)>;
using NativePropertySetFn = std::function<void(VirtualMachine*, const RuntimeValue&)>;

struct NativePropertyDefinition {
    NomadId id = NOMAD_INVALID_ID;
    const Type* objectType = nullptr;
    NomadString name;
    const Type* valueType = nullptr;
    NativePropertyGetFn getFn;
    NativePropertySetFn setFn;
    NomadDocField;
};

struct NativeIndexerDefinition {
    NomadId id = NOMAD_INVALID_ID;
    const ObjectType* objectType = nullptr;
    std::vector<const Type*> indexTypes;
    const Type* valueType = nullptr;
    NativeFunctionFn getFn;
    NativeFunctionFn setFn;
    NomadDocField;
};

} // namespace nomad
