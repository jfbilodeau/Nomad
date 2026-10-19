// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ScriptFunction.hpp>

namespace nomad {

ScriptFunction::ScriptFunction(
    const NomadId id,
    NomadString name,
    NomadString path,
    NomadString source
):
    ScriptCallable(
        id,
        std::move(path),
        std::move(source),
        std::move(name),
        std::vector<Parameter>{},
        nullptr,
        NomadString{}
    )
{
}

} // namespace nomad
