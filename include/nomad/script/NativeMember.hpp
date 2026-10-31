// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/Member.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Property.hpp>
#include <nomad/script/RuntimeValue.hpp>
#include <nomad/script/Type.hpp>

#include <functional>
#include <vector>

namespace nomad {

class Type;
class ClassType;
class VirtualMachine;

using NativePropertyGetFn = std::function<void(VirtualMachine*, RuntimeValue&)>;
using NativePropertySetFn = std::function<void(VirtualMachine*, const RuntimeValue&)>;

struct ParameterDefinition {
    TypeId typeId;
    NomadString name;
    NomadString documentation;
};

struct NativeMethodDefinition {
    NomadString name;
    std::vector<ParameterDefinition> parameters;
    TypeId returnTypeId;
    NativeFunctionFn function;
    NomadString documentation;
};

struct NativeConstructorDefinition {
    std::vector<ParameterDefinition> parameters;
    NativeFunctionFn function;
    NomadString documentation;
};

struct NativePropertyDefinition {
    const Type* valueType = nullptr;
    NomadString name;
    NativePropertyGetFn getter;
    NativePropertySetFn setter;
    NomadString documentation;
};

struct NativePropertyInfo {
    PropertyId id;
    const Type* classType = nullptr;
    NomadString name;
    const Type* valueType = nullptr;
    NativePropertyGetFn getFn;
    NativePropertySetFn setFn;
    NomadDocField;
};

DeclareId(IndexerId);

struct NativeIndexerInfo {
    IndexerId id;
    const ClassType* classType = nullptr;
    std::vector<const Type*> indexTypes;
    const Type* valueType = nullptr;
    NativeFunctionFn getFn;
    NativeFunctionFn setFn;
    NomadDocField;
};

} // namespace nomad
