// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NativeCallable.hpp>
#include <nomad/script/NativeMember.hpp>
#include <nomad/script/Type.hpp>

#include <vector>

namespace nomad {

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
    ConstructorFactory factory;
    NomadString documentation;
};

struct NativePropertyDefinition {
    TypeId valueTypeId;
    NomadString name;
    NativePropertyGetFn getter;
    NativePropertySetFn setter;
    NomadString documentation;
};

struct NativeIndexerDefinition {
    TypeId valueTypeId;
    std::vector<TypeId> indexTypes;
    NativeFunctionFn getter;
    NativeFunctionFn setter;
    NomadString documentation;
};

struct NativeInterableDefinition {
    TypeId valueTypeId;
    NativeMethodDefinition factory;
};

} // namespace nomad
