// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <FailingArgumentCopyType.hpp>

#include <nomad/script/RuntimeValue.hpp>

#include <utility>

namespace nomad::test {

FailingArgumentCopyType::FailingArgumentCopyType(
    Runtime* runtime,
    const TypeId id,
    NomadString name,
    const ClassMode mode,
    NomadIndex& copyCount,
    const NomadIndex& failCopyAt
):
    ClassType(runtime, id, std::move(name), mode),
    m_copyCount(copyCount),
    m_failCopyAt(failCopyAt)
{}

void FailingArgumentCopyType::copyValue(
    const RuntimeValue* source,
    RuntimeValue* destination
) const {
    ClassType::copyValue(source, destination);
    if (++m_copyCount == m_failCopyAt) {
        freeValue(destination);
        throw NomadException("Argument copy failed");
    }
}

} // namespace nomad::test
