// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/ScriptCallable.hpp>

namespace nomad {

class ScriptMethod final : public ScriptCallable<Method> {
public:
    ScriptMethod(
        NomadString name,
        const Type* objectType,
        std::vector<Parameter> parameters,
        const Type* returnType,
        NomadString path,
        NomadString source
    );

    [[nodiscard]] const ScriptMethod* asScriptMethod() const override;
};

} // namespace nomad
