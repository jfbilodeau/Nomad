// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/ValueType.hpp>

#include <utility>

namespace nomad {

ValueType::ValueType(Runtime* runtime, TypeId id, NomadString documentation):
    Type(runtime, id, std::move(documentation))
{}

TypeStorage ValueType::getStorage() const {
    return TypeStorage::Value;
}

NomadIndex ValueType::getSize() const {
    return 1;
}

void ValueType::emitFreeParameter(OpCodeEmitter&, NomadId) const {
}

} // namespace nomad
