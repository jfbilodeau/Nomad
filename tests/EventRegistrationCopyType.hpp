// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/ClassType.hpp>

#include <functional>

namespace nomad::test {

class EventRegistrationCopyType final : public ClassType {
public:
    EventRegistrationCopyType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        std::function<void()> registration
    );

    void copyValue(const RuntimeValue* source, RuntimeValue* destination) const override;

private:
    std::function<void()> m_registration;
};

} // namespace nomad::test
