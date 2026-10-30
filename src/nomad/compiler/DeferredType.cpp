// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/DeferredType.hpp>

#include <utility>

namespace nomad {

DeferredType::DeferredType(Runtime* runtime, TypeReference reference) :
    Type(runtime, TypeId{}), m_reference(std::move(reference)) {
    if (runtime == nullptr || m_reference.name.empty()) {
        throw NomadException("A deferred type requires a runtime and a nonempty type name");
    }
}

const TypeReference& DeferredType::getReference() const {
    return m_reference;
}

NomadString DeferredType::getTypeName() const {
    return m_reference.name;
}

TypeStorage DeferredType::getStorage() const {
    reportUnresolved();
}

void DeferredType::initValue(RuntimeValue&) const {
    reportUnresolved();
}

void DeferredType::freeValue(RuntimeValue&) const {
    reportUnresolved();
}

void DeferredType::copyValue(const RuntimeValue&, RuntimeValue&) const {
    reportUnresolved();
}

void DeferredType::toString(const RuntimeValue&, NomadString&) const {
    reportUnresolved();
}

void DeferredType::reportUnresolved() const {
    throw NomadException("Deferred type '" + m_reference.name + "' has not been resolved");
}

} // namespace nomad
