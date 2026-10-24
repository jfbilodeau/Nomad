#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class ReadOnlyMapType final : public ObjectType {
public:
    ReadOnlyMapType(
        Runtime* runtime,
        NomadString name,
        ObjectTypeMode mode,
        const Type* keyType,
        const Type* valueType
    );

    void initValue(RuntimeValue& value) const override;
};

} // namespace nomad