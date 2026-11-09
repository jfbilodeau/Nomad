// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Closure.hpp>

#include <nomad/script/ScriptCallable.hpp>

#include <stdexcept>
#include <utility>

namespace nomad {

Closure::Closure(
    const ScriptFunction* function,
    std::vector<RuntimeValue>&& captures
):
    m_function(function)
{
    if (m_function == nullptr) {
        throw std::invalid_argument("Closure requires a script function");
    }

    auto offset = NomadIndex{0};

    for (const auto& capture : m_function->getCaptures()) {
        if (capture.type == nullptr || capture.type->isVoid()) {
            throw std::invalid_argument("Closure captures require resolved non-void types");
        }

        const auto size = capture.type->getSize();
        if (size > captures.size() - offset) {
            throw std::invalid_argument("Closure storage does not match its capture layout");
        }

        offset += size;
    }

    if (offset != captures.size()) {
        throw std::invalid_argument("Closure storage does not match its capture layout");
    }

    m_captureCount = m_function->getCaptureCount();
    m_captureStorage = std::move(captures);
}

Closure::~Closure() {
    auto offset = NomadIndex{0};

    for (auto index = NomadIndex{0}; index < m_captureCount; ++index) {
        const auto* type = m_function->getCaptureType(toNomadId(index));
        const auto size = type->getSize();
        if (size != 0) {
            type->freeValue(m_captureStorage.data() + offset);
        }

        offset += size;
    }
}

NomadIndex Closure::getCaptureCount() const {

    return m_captureCount;
}

const RuntimeValue& Closure::getCaptureValue(
    const NomadIndex captureIndex
) const {
    const auto* type = getCaptureType(captureIndex);
    
    if (type->isStruct() || type->getSize() != 1) {
        throw std::invalid_argument(
            "Capture value requires scalar storage; use getCaptureStorage for structs"
        );
    }

    return getCaptureStorage(captureIndex)[0];
}

std::span<const RuntimeValue> Closure::getCaptureStorage(
    const NomadIndex captureIndex
) const {
    const auto* type = getCaptureType(captureIndex);
    const auto size = type->getSize();
    if (size == 0) {

        return {};
    }

    auto offset = NomadIndex{0};
    for (auto index = NomadIndex{0}; index < captureIndex; ++index) {
        offset += m_function->getCaptureType(toNomadId(index))->getSize();
    }

    return {m_captureStorage.data() + offset, size};
}

const Type* Closure::getCaptureType(const NomadIndex captureIndex) const {
    if (captureIndex >= m_captureCount) {
        throw std::out_of_range("Closure capture index is out of range");
    }

    return m_function->getCaptureType(toNomadId(captureIndex));
}

} // namespace nomad
