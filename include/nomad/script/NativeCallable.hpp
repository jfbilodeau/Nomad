// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/Callable.hpp>
#include <nomad/script/RuntimeValue.hpp>

#include <functional>

namespace nomad {

class NomadObject;
class VirtualMachine;

using NativeFunctionFn = std::function<void(VirtualMachine*)>;

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
        NativeFunctionFn fn,
        std::vector<Parameter> parameters,
        NomadString doc
    );

    [[nodiscard]] const NativeConstructor* asNativeConstructor() const override { return this; }
    [[nodiscard]] const Type* getConstructedType() const override;
    [[nodiscard]] const NativeFunctionFn& getFn() const { return m_fn; }

private:
    NativeFunctionFn m_fn;
};

} // namespace nomad
