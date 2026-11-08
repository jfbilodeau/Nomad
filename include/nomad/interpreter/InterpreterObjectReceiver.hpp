// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/interpreter/InterpreterReceiver.hpp>

namespace nomad {

class ClassType;
class NomadObject;

class InterpreterObjectReceiver final : public InterpreterReceiver {
public:
    // Retains a non-null object; the interpreter must outlive this wrapper.
    InterpreterObjectReceiver(
        const Interpreter& interpreter,
        const ClassType& type,
        NomadObject* object
    );
    ~InterpreterObjectReceiver() override;

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
    NomadObject* m_object;
};

} // namespace nomad
