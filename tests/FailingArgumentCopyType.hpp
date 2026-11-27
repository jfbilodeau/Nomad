// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/ClassType.hpp>

namespace nomad::test {

class FailingArgumentCopyType final : public ClassType {
public:
    FailingArgumentCopyType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        NomadIndex& copyCount,
        const NomadIndex& failCopyAt
    );

    void copyValue(const RuntimeValue* source, RuntimeValue* destination) const override;

private:
    NomadIndex& m_copyCount;
    const NomadIndex& m_failCopyAt;
};

} // namespace nomad::test
