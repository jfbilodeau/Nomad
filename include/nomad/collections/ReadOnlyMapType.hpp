#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class ReadOnlyMapType final : public ObjectType {
public:
    ReadOnlyMapType(const Type* keyType, const Type* valueType);

    void initValue(RuntimeValue& value) const override;
};

} // namespace nomad