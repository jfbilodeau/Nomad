#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class ReadOnlyMapType final : public ClassType {
public:
    ReadOnlyMapType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        const Type* keyType,
        const Type* valueType
    );

    void initValue(RuntimeValue& value) const override;
};

} // namespace nomad