// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/ClassType.hpp>

#include <functional>

namespace nomad::test {

class ReentrantRegistrationType final : public ClassType {
public:
    ReentrantRegistrationType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        std::function<void(Runtime*, TypeId, const NomadString&)> registration,
        bool& destroyed
    );
    ~ReentrantRegistrationType() override;

private:
    bool& m_destroyed;
};

} // namespace nomad::test
