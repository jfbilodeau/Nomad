// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Callable.hpp>

namespace nomad {

ParameterDefinition defParameter(const NomadString& name, const Type* type, const NomadString& doc) {
    return {
        name,
        type,
        doc
    };
}

} // namespace nomad
