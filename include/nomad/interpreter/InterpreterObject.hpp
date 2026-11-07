// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/interpreter/InterpreterComposite.hpp>

#pragma once

namespace nomad {

class NomadObject;

class InterpreterObject final : public InterpreterComposite {
public:
    // The object and interpreter must outlive this borrowed wrapper.
    InterpreterObject(const Interpreter& interpreter, const Type& type, NomadObject* object);

    using InterpreterComposite::getProperty;
    using InterpreterComposite::setProperty;
    [[nodiscard]] InterpreterValue getValue() const override;
    [[nodiscard]] InterpreterValue getProperty(PropertyId propertyId) const override;
    void setProperty(PropertyId propertyId, const InterpreterValue& value) override;

private:
    NomadObject* m_object;
};

} // namespace nomad
