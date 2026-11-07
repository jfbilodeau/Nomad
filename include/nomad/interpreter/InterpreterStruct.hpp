// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/interpreter/InterpreterComposite.hpp>

#include <variant>

namespace nomad {

class StructType;

class InterpreterStruct final : public InterpreterComposite {
public:
    // Borrows getSize() live slots; empty struct storage may be null.
    InterpreterStruct(const Interpreter& interpreter, const StructType& type, RuntimeValue* storage);
    // Owns an independent value when passed an lvalue; temporaries transfer ownership.
    InterpreterStruct(const Interpreter& interpreter, InterpreterValue value);

    using InterpreterComposite::getProperty;
    using InterpreterComposite::setProperty;
    [[nodiscard]] InterpreterValue getValue() const override;
    [[nodiscard]] InterpreterValue getProperty(PropertyId propertyId) const override;
    void setProperty(PropertyId propertyId, const InterpreterValue& value) override;

private:
    void replaceValue(InterpreterValue value);

    std::variant<RuntimeValue*, InterpreterValue> m_storage;
};

} // namespace nomad
