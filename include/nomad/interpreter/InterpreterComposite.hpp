// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/interpreter/InterpreterValue.hpp>

#include <nomad/script/Property.hpp>

namespace nomad {

class Interpreter;
class Type;

// AST evaluation infrastructure; host invocation APIs continue to use InterpreterValue.
class InterpreterComposite {
public:
    InterpreterComposite(const InterpreterComposite&) = delete;
    InterpreterComposite& operator=(const InterpreterComposite&) = delete;
    virtual ~InterpreterComposite();

    [[nodiscard]] const Type* getType() const;
    [[nodiscard]] virtual InterpreterValue getValue() const = 0;
    [[nodiscard]] virtual InterpreterValue getProperty(PropertyId propertyId) const = 0;
    virtual void setProperty(PropertyId propertyId, const InterpreterValue& value) = 0;
    // Paths traverse stored struct fields beneath the selected property.
    [[nodiscard]] InterpreterValue getProperty(NomadStringView propertyPath) const;
    void setProperty(NomadStringView propertyPath, const InterpreterValue& value);

protected:
    InterpreterComposite(const Interpreter& interpreter, const Type& type);
    [[nodiscard]] const Interpreter& getInterpreter() const;

private:
    [[nodiscard]] PropertyId resolveProperty(NomadStringView name) const;

    const Interpreter& m_interpreter;
    const Type& m_type;
};

} // namespace nomad
