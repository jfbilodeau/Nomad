// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <functional>
#include <vector>

namespace nomad {

class Type;
class VirtualMachine;

enum class NativeMemberType {
    Unknown = 1,
    Property,
    Method,
};

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

using NativeMethodDefinition = NativeFunctionDefinition;

} // namespace nomad
