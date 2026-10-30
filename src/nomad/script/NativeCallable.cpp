// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NativeCallable.hpp>

#include <nomad/script/Runtime.hpp>

#include <algorithm>
#include <utility>

namespace nomad {

NativeFunction::NativeFunction(
    NomadString name,
    NativeFunctionFn fn,
    std::vector<Parameter> parameters,
    const Type* returnType,
    NomadString doc,
    std::optional<TypeReference> returnTypeReference
):
    Function(CallableSource::Native, std::move(name), std::move(parameters), returnType, std::move(doc)),
    m_fn(std::move(fn)),
    m_returnTypeReference(std::move(returnTypeReference)),
    m_signatureResolved(!m_returnTypeReference && std::ranges::none_of(getParameters(), [](const auto& parameter) {
        return parameter.typeReference.has_value();
    }))
{
}

const NativeFunctionFn& NativeFunction::getFn() const {
    if (!m_signatureResolved) {
        throw NomadException("Native function '" + getName() + "' has an unresolved signature");
    }
    return m_fn;
}

bool NativeFunction::isSignatureResolved() const {
    return m_signatureResolved;
}

NomadString NativeFunction::getReturnTypeName() const {
    if (getReturnType() != nullptr) {
        return getReturnType()->getTypeName();
    }
    return m_returnTypeReference ? m_returnTypeReference->name : "unknown";
}

void NativeFunction::resolveSignatureTypes(Runtime& runtime) {
    auto parameters = getParameters();
    for (auto& parameter : parameters) {
        if (parameter.typeReference) {
            parameter.type = runtime.getTypeByName(parameter.typeReference->name);
            if (parameter.type == nullptr || parameter.type->isVoid()) {
                throw NomadException("Cannot resolve native function '" + getName() + "' parameter '" +
                    parameter.name + "': unknown or invalid type '" + parameter.typeReference->name + "'");
            }
        }
    }
    auto* returnType = getReturnType();
    if (m_returnTypeReference) {
        returnType = runtime.getTypeByName(m_returnTypeReference->name);
        if (returnType == nullptr || (returnType->isInternal() && !returnType->isVoid())) {
            throw NomadException("Cannot resolve native function '" + getName() +
                "' return type: unknown or invalid type '" + m_returnTypeReference->name + "'");
        }
    }
    getEditableParameters() = std::move(parameters);
    setReturnType(returnType);
}

NativeMethod::NativeMethod(
    NomadString name,
    const Type* classType,
    NativeFunctionFn fn,
    std::vector<Parameter> parameters,
    const Type* returnType,
    NomadString doc
):
    Method(CallableSource::Native, std::move(name), classType, std::move(parameters), returnType, std::move(doc)),
    m_fn(std::move(fn))
{
}

NativeConstructor::NativeConstructor(
    NomadString name,
    const Type* classType,
    const Type* createdType,
    NativeFunctionFn fn,
    std::vector<Parameter> parameters,
    NomadString doc
):
    Constructor(CallableSource::Native, std::move(name), classType, std::move(parameters), createdType, std::move(doc)),
    m_fn(std::move(fn))
{
}

const Type* NativeConstructor::getConstructedType() const {
    return getReturnType();
}

} // namespace nomad
