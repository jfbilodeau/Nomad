// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Callable.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <functional>
#include <span>

namespace nomad {

class NomadObject;
class VirtualMachine;

using NativeFunctionFn = std::function<void(VirtualMachine*)>;
using ConstructorFactory = std::function<NomadObject*(std::span<const RuntimeValue>)>;

class NativeFunction : public Function {
public:
    NativeFunction(
        NomadString name,
        NativeFunctionFn fn,
        std::vector<Parameter> parameters,
        const Type* returnType,
        NomadString doc
    );

    [[nodiscard]] const NativeFunction* asNativeFunction() const override { return this; }
    [[nodiscard]] const NativeFunctionFn& getFn() const { return m_fn; }

private:
    NativeFunctionFn m_fn;
};

class NativeMethod : public Method {
public:
    NativeMethod(
        NomadString name,
        const Type* objectType,
        NativeFunctionFn fn,
        std::vector<Parameter> parameters,
        const Type* returnType,
        NomadString doc
    );

    [[nodiscard]] const NativeMethod* asNativeMethod() const override { return this; }
    [[nodiscard]] const NativeFunctionFn& getFn() const { return m_fn; }

private:
    NativeFunctionFn m_fn;
};

class NativeConstructor : public Constructor {
public:
    NativeConstructor(
        NomadString name,
        const Type* objectType,
        const Type* createdType,
        ConstructorFactory factory,
        std::vector<Parameter> parameters,
        NomadString doc
    );

    [[nodiscard]] const NativeConstructor* asNativeConstructor() const override { return this; }
    [[nodiscard]] const ConstructorFactory& getFactory() const { return m_factory; }

private:
    ConstructorFactory m_factory;
};

} // namespace nomad
