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
    std::optional<TypeReference> returnTypeReference,
    const bool deferSignatureValidation
):
    Function(CallableSource::Native, std::move(name), std::move(parameters), returnType, std::move(doc)),
    m_fn(std::move(fn)),
    m_returnTypeReference(std::move(returnTypeReference)),
    m_signatureResolved(!deferSignatureValidation && !m_returnTypeReference &&
        std::ranges::none_of(getParameters(), [](const auto& parameter) {
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

const std::optional<TypeReference>& NativeFunction::getReturnTypeReference() const {
    return m_returnTypeReference;
}

bool NativeFunction::isRegisteredType(const Type* type) const {
    if (type == nullptr || type->getRuntime() == nullptr) {
        return false;
    }
    const auto* runtime = type->getRuntime();
    return runtime->getType(type->getId()) == type && runtime->getNativeFunction(getId()) == this;
}

void NativeFunction::resolveParameterType(const NomadIndex index, const Type* type) {
    if (m_signatureResolved || index >= getParameterCount()) {
        throw NomadException("Cannot resolve native function '" + getName() + "': invalid parameter target or state");
    }
    auto& parameter = getEditableParameters()[index];
    if (!isRegisteredType(type) || type->isVoid() || !parameter.typeReference ||
        parameter.typeReference->name != type->getTypeName()) {
        throw NomadException("Cannot resolve native function '" + getName() + "' parameter '" +
            parameter.name + "': invalid type or target '" + parameter.getTypeName() + "'");
    }
    parameter.type = type;
}

void NativeFunction::resolveReturnType(const Type* type) {
    if (m_signatureResolved || !isRegisteredType(type) || (type->isInternal() && !type->isVoid()) ||
        !m_returnTypeReference || m_returnTypeReference->name != type->getTypeName()) {
        throw NomadException("Cannot resolve native function '" + getName() +
            "' return type: invalid type or target '" + getReturnTypeName() + "'");
    }
    setReturnType(type);
}

void NativeFunction::completeSignatureResolution() {
    if (!isRegisteredType(getReturnType()) ||
        (getReturnType()->isInternal() && !getReturnType()->isVoid()) ||
        (m_returnTypeReference && m_returnTypeReference->name != getReturnType()->getTypeName()) ||
        std::ranges::any_of(getParameters(), [this](const auto& parameter) {
            return !isRegisteredType(parameter.type) || parameter.type->isVoid() ||
                (parameter.typeReference && parameter.typeReference->name != parameter.type->getTypeName());
        })) {
        throw NomadException("Cannot complete native function '" + getName() + "': unresolved or invalid signature");
    }
    for (auto& parameter : getEditableParameters()) {
        parameter.typeReference.reset();
    }
    m_returnTypeReference.reset();
    m_signatureResolved = true;
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
