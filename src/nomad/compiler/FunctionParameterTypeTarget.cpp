// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/FunctionParameterTypeTarget.hpp>

#include <nomad/script/Runtime.hpp>

#include <utility>

namespace nomad {

FunctionParameterTypeTarget::FunctionParameterTypeTarget(
    Runtime& runtime,
    const CallableId id,
    const NomadIndex index,
    TypeReference type
) : m_runtime(&runtime), m_id(id), m_index(index), m_type(std::move(type)) {
    const auto* function = runtime.getNativeFunction(id);
    if (function == nullptr || index >= function->getParameterCount() || m_type.name.empty()) {
        throw NomadException("Invalid deferred function parameter target");
    }
}

const TypeReference& FunctionParameterTypeTarget::getDeferredType() const {
    return m_type;
}

NomadString FunctionParameterTypeTarget::getDescription() const {
    const auto* function = m_runtime->getNativeFunction(m_id);
    return "native function '" + function->getName() + "' parameter '" +
        function->getParameters()[m_index].name + "'";
}

void FunctionParameterTypeTarget::resolve(const Type* type) {
    auto* function = m_runtime->getMutableNativeFunction(m_id);
    if (type == nullptr || type->getTypeName() != m_type.name || function == nullptr) {
        throw NomadException("Cannot resolve " + getDescription() + ": invalid type or target '" + m_type.name + "'");
    }
    if (!function->isSignatureResolved() && function->getParameters()[m_index].type == type) {
        return;
    }
    function->resolveParameterType(m_index, type);
}

} // namespace nomad
