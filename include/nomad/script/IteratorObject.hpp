// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>
#include <nomad/script/RuntimeValue.hpp>
#include <nomad/script/Type.hpp>

namespace nomad {

class IteratorObject : public NomadObject {
public:
    explicit IteratorObject(const Type* valueType);
    ~IteratorObject() override;
    [[nodiscard]] bool moveNext();
    [[nodiscard]] const RuntimeValue& getCurrent() const;

protected:
    // Write an owned value into the empty output only when returning true.
    [[nodiscard]] virtual bool onMoveNext(RuntimeValue& value) = 0;

private:
    const Type* m_valueType;
    RuntimeValue m_current;
    bool m_valid = false;
    bool m_completed = false;
};

struct IterableDefinition {
    const Type* valueType;
    const ObjectTypeBase* iteratorType;
    NomadId factoryId;
};

} // namespace nomad
