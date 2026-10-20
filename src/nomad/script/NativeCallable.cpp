// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NativeCallable.hpp>

namespace nomad {

NativeFunction::NativeFunction(
    NomadString name,
    NativeFunctionFn fn,
    std::vector<Parameter> parameters,
    const Type* returnType,
    NomadString doc
):
    Function(CallableSource::Native, std::move(name), std::move(parameters), returnType, std::move(doc)),
    m_fn(std::move(fn))
{
}

NativeMethod::NativeMethod(
    NomadString name,
    const Type* objectType,
    NativeFunctionFn fn,
    std::vector<Parameter> parameters,
    const Type* returnType,
    NomadString doc
):
    Method(CallableSource::Native, std::move(name), objectType, std::move(parameters), returnType, std::move(doc)),
    m_fn(std::move(fn))
{
}

NativeConstructor::NativeConstructor(
    NomadString name,
    const Type* objectType,
    const Type* createdType,
    ConstructorFactory factory,
    std::vector<Parameter> parameters,
    NomadString doc
):
    Constructor(CallableSource::Native, std::move(name), objectType, std::move(parameters), createdType, std::move(doc)),
    m_factory(std::move(factory))
{
}

} // namespace nomad
