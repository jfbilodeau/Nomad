#pragma once

#include <nomad/script/ClassType.hpp>

namespace nomad {

class ReadOnlyListType final : public ClassType {
public:
    ReadOnlyListType(
        Runtime* runtime,
        TypeId id,
        NomadString name,
        ClassMode mode,
        const Type* elementType
    );

    void initValue(RuntimeValue& value) const override;
};

} // namespace nomad