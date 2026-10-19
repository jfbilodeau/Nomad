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
class ObjectTypeBase;
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

using NativeIndexerGetFn1 = std::function<void(VirtualMachine*, const RuntimeValue&, RuntimeValue&)>;
using NativeIndexerSetFn1 = std::function<void(VirtualMachine*, const RuntimeValue&, const RuntimeValue&)>;

struct NativeIndexerDefinition1 {
    NomadId id = NOMAD_INVALID_ID;
    const ObjectTypeBase* objectType = nullptr;
    const Type* indexType = nullptr;
    const Type* valueType = nullptr;
    NativeIndexerGetFn1 getFn;
    NativeIndexerSetFn1 setFn;
    NomadDocField;
};

} // namespace nomad
