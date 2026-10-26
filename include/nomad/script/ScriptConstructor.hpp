// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/ScriptCallable.hpp>

namespace nomad {

class ScriptConstructor final : public ScriptCallable<Constructor> {
public:
    ScriptConstructor(
        const Type* objectType,
        std::vector<Parameter> parameters,
        NomadString path,
        NomadString source
    );

    [[nodiscard]] const ScriptConstructor* asScriptConstructor() const override;
    [[nodiscard]] const Type* getConstructedType() const override;
};

} // namespace nomad
