// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <EventRegistrationCopyType.hpp>

#include <utility>

namespace nomad::test {

EventRegistrationCopyType::EventRegistrationCopyType(
    Runtime* runtime,
    const TypeId id,
    NomadString name,
    const ClassMode mode,
    std::function<void()> registration
):
    ClassType(runtime, id, std::move(name), mode),
    m_registration(std::move(registration))
{}

void EventRegistrationCopyType::copyValue(
    const RuntimeValue* source,
    RuntimeValue* destination
) const {
    m_registration();
    ClassType::copyValue(source, destination);
}

} // namespace nomad::test
