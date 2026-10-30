// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/NativePropertyTypeTarget.hpp>

#include <nomad/script/NativeProperty.hpp>
#include <nomad/script/Runtime.hpp>

#include <utility>

namespace nomad {

NativePropertyTypeTarget::NativePropertyTypeTarget(Runtime& runtime, const PropertyId id, TypeReference type) :
    m_runtime(&runtime), m_id(id), m_type(std::move(type)) {
    if (runtime.getNativeProperty(id) == nullptr || m_type.name.empty()) {
        throw NomadException("Invalid deferred native property target");
    }
}

const TypeReference& NativePropertyTypeTarget::getDeferredType() const {
    return m_type;
}

NomadString NativePropertyTypeTarget::getDescription() const {
    const auto* property = m_runtime->getNativeProperty(m_id);
    return "native property '" + property->getClassType()->getTypeName() + "." + property->getName() + "' value type";
}

void NativePropertyTypeTarget::resolve(const Type* type) {
    auto* property = m_runtime->getMutableNativeProperty(m_id);
    if (type == nullptr || type->getTypeName() != m_type.name || property == nullptr) {
        throw NomadException("Cannot resolve " + getDescription() + ": invalid type or target '" + m_type.name + "'");
    }
    if (!property->isTypeResolved() && property->getValueType() == type) {
        return;
    }
    property->resolveValueType(type);
}

} // namespace nomad
