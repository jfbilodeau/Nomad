// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/interpreter/InterpreterReceiver.hpp>

namespace nomad {

class NomadObject;

class InterpreterObjectReceiver final : public InterpreterReceiver {
public:
    // The object and interpreter must outlive this borrowed wrapper.
    InterpreterObjectReceiver(const Interpreter& interpreter, const Type& type, NomadObject* object);

    using InterpreterReceiver::getProperty;
    using InterpreterReceiver::setProperty;
    [[nodiscard]] InterpreterValue getValue() const override;
    [[nodiscard]] InterpreterValue getProperty(PropertyId propertyId) const override;
    void setProperty(PropertyId propertyId, const InterpreterValue& value) override;

private:
    NomadObject* m_object;
};

} // namespace nomad
