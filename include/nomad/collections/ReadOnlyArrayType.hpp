#pragma once

#include <nomad/collections/ArrayType.hpp>

namespace nomad {

class ReadOnlyArrayType final : public ArrayType {
public:
    ReadOnlyArrayType(
        Runtime* runtime,
        NomadString name,
        ObjectTypeMode mode,
        const Type* elementType,
        NomadIndex dimensionCount
    );

protected:
    [[nodiscard]] ArrayObject* createEmptyArray() const override;
};

} // namespace nomad