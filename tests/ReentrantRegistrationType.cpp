// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <ReentrantRegistrationType.hpp>

#include <utility>

namespace nomad::test {

ReentrantRegistrationType::ReentrantRegistrationType(
    Runtime* runtime,
    const TypeId id,
    NomadString name,
    const ClassMode mode,
    std::function<void(Runtime*, TypeId, const NomadString&)> registration,
    bool& destroyed
):
    ClassType(runtime, id, std::move(name), mode),
    m_destroyed(destroyed)
{
    registration(runtime, id, getTypeName());
}

ReentrantRegistrationType::~ReentrantRegistrationType() {
    m_destroyed = true;
}

} // namespace nomad::test
