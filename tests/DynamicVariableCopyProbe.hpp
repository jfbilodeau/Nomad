// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

namespace nomad {
class RuntimeValue;
class VirtualMachine;
}

namespace nomad::test {

struct DynamicVariableCopyCounts {
    NomadIndex copies = 0;
    NomadIndex moves = 0;
    NomadIndex invocations = 0;
};

class DynamicVariableCopyProbe {
public:
    explicit DynamicVariableCopyProbe(DynamicVariableCopyCounts& counts);
    DynamicVariableCopyProbe(const DynamicVariableCopyProbe& other) noexcept;
    DynamicVariableCopyProbe(DynamicVariableCopyProbe&& other) noexcept;

    void operator()(VirtualMachine* machine, const RuntimeValue& value) const;
    void operator()(VirtualMachine* machine, RuntimeValue& value) const;

private:
    DynamicVariableCopyCounts* m_counts;
};

} // namespace nomad::test
