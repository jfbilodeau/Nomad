// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/interpreter/InterpreterReceiver.hpp>

namespace nomad {

class StructType;

class InterpreterStructReceiver final : public InterpreterReceiver {
public:
    // Borrows getSize() live slots; empty struct storage may be null.
    InterpreterStructReceiver(const Interpreter& interpreter, const StructType& type, RuntimeValue* storage);
    using InterpreterReceiver::getProperty;
    using InterpreterReceiver::setProperty;
    [[nodiscard]] InterpreterValue getValue() const override;
    [[nodiscard]] InterpreterValue getProperty(PropertyId propertyId) const override;
    void setProperty(PropertyId propertyId, const InterpreterValue& value) override;
    [[nodiscard]] InterpreterValue callMethod(
        CallableId methodId,
        std::span<const InterpreterValue> arguments
    ) override;

private:
    RuntimeValue* m_storage;
};

} // namespace nomad
