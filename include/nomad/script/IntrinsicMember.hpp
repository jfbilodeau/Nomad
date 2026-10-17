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

enum class IntrinsicMemberType {
    Unknown = 1,
    Property,
    Method,
};

using IntrinsicPropertyGetFn = std::function<void(VirtualMachine*, const RuntimeValue&, RuntimeValue&)>;
using IntrinsicPropertySetFn = std::function<void(VirtualMachine*, RuntimeValue&, const RuntimeValue&)>;

struct IntrinsicPropertyDefinition {
    NomadId id = NOMAD_INVALID_ID;
    const Type* objectType = nullptr;
    NomadString name;
    const Type* valueType = nullptr;
    IntrinsicPropertyGetFn getFn;
    IntrinsicPropertySetFn setFn;
    NomadDocField;
};

using IntrinsicMethodDefinition = NativeFunctionDefinition;

} // namespace nomad
