// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

namespace nomad {

class CompilerException : public NomadException {
public:
    explicit CompilerException(const NomadString& message) : NomadException(message) {}
};

}
