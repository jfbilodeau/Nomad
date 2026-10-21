#pragma once

#include <nomad/script/Type.hpp>

namespace nomad {

class ReadOnlyListType final : public ObjectType {
public:
    explicit ReadOnlyListType(const Type* elementType);

    void initValue(RuntimeValue& value) const override;
};

} // namespace nomad