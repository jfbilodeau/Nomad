// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/FunctionReturnTypeTarget.hpp>

#include <nomad/script/Runtime.hpp>

#include <utility>

namespace nomad {

FunctionReturnTypeTarget::FunctionReturnTypeTarget(Runtime& runtime, const CallableId id, TypeReference type) :
    m_runtime(&runtime), m_id(id), m_type(std::move(type)) {
    if (runtime.getNativeFunction(id) == nullptr || m_type.name.empty()) {
        throw NomadException("Invalid deferred function return target");
    }
}

const TypeReference& FunctionReturnTypeTarget::getDeferredType() const {
    return m_type;
}

NomadString FunctionReturnTypeTarget::getDescription() const {
    return "native function '" + m_runtime->getNativeFunction(m_id)->getName() + "' return type";
}

void FunctionReturnTypeTarget::resolve(const Type* type) {
    auto* function = m_runtime->getMutableNativeFunction(m_id);
    if (type == nullptr || type->getTypeName() != m_type.name || function == nullptr) {
        throw NomadException("Cannot resolve " + getDescription() + ": invalid type or target '" + m_type.name + "'");
    }
    if (!function->isSignatureResolved() && function->getReturnType() == type) {
        return;
    }
    function->resolveReturnType(type);
}

} // namespace nomad
