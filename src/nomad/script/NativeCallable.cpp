// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/NativeCallable.hpp>

#include <nomad/compiler/DeferredType.hpp>

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
    const bool deferSignatureValidation
):
    Function(CallableSource::Native, std::move(name), std::move(parameters), returnType, std::move(doc)),
    m_fn(std::move(fn)),
    m_signatureResolved(!deferSignatureValidation && dynamic_cast<const DeferredType*>(returnType) == nullptr &&
        std::ranges::none_of(getParameters(), [](const auto& parameter) {
        return dynamic_cast<const DeferredType*>(parameter.type) != nullptr;
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
    return "unknown";
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
    const auto* placeholder = dynamic_cast<const DeferredType*>(parameter.type);
    if (!isRegisteredType(type) || type->isVoid() || placeholder == nullptr ||
        placeholder->getTypeName() != type->getTypeName()) {
        throw NomadException("Cannot resolve native function '" + getName() + "' parameter '" +
            parameter.name + "': invalid type or target '" + parameter.getTypeName() + "'");
    }
    parameter.type = type;
}

void NativeFunction::resolveReturnType(const Type* type) {
    const auto* placeholder = dynamic_cast<const DeferredType*>(getReturnType());
    if (m_signatureResolved || !isRegisteredType(type) || (type->isInternal() && !type->isVoid()) ||
        placeholder == nullptr || placeholder->getTypeName() != type->getTypeName()) {
        throw NomadException("Cannot resolve native function '" + getName() +
            "' return type: invalid type or target '" + getReturnTypeName() + "'");
    }
    setReturnType(type);
}

void NativeFunction::completeSignatureResolution() {
    if (!isRegisteredType(getReturnType()) ||
        (getReturnType()->isInternal() && !getReturnType()->isVoid()) ||
        std::ranges::any_of(getParameters(), [this](const auto& parameter) {
            return !isRegisteredType(parameter.type) || parameter.type->isVoid();
        })) {
        throw NomadException("Cannot complete native function '" + getName() + "': unresolved or invalid signature");
    }
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
