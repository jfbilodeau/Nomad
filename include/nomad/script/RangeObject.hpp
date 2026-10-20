// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>

namespace nomad {

class RangeObject final : public NomadObject {
public:
    RangeObject(
        NomadInteger start,
        NomadInteger stop,
        NomadInteger step
    );

    [[nodiscard]] NomadInteger getStart() const;
    [[nodiscard]] NomadInteger getStop() const;
    [[nodiscard]] NomadInteger getStep() const;

private:
    NomadInteger m_start;
    NomadInteger m_stop;
    NomadInteger m_step;
};

} // namespace nomad
