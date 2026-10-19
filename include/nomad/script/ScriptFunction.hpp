// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/ScriptCallable.hpp>

namespace nomad {

class ScriptFunction : public ScriptCallable<Function> {
public:
    ScriptFunction(
        NomadId id,
        NomadString name,
        NomadString path,
        NomadString source
    );
};

} // namespace nomad
