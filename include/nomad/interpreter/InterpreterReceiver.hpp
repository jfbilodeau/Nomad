// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/interpreter/InterpreterValue.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/Property.hpp>

namespace nomad {

class Interpreter;
class Type;

// AST evaluation infrastructure; host invocation APIs continue to use InterpreterValue.
class InterpreterReceiver {
public:
    InterpreterReceiver(const InterpreterReceiver&) = delete;
    InterpreterReceiver& operator=(const InterpreterReceiver&) = delete;
    virtual ~InterpreterReceiver();

    [[nodiscard]] const Type* getType() const;
    [[nodiscard]] virtual InterpreterValue getValue() const = 0;
    [[nodiscard]] virtual InterpreterValue getProperty(PropertyId propertyId) const = 0;
    virtual void setProperty(PropertyId propertyId, const InterpreterValue& value) = 0;
    [[nodiscard]] virtual InterpreterValue callMethod(
        CallableId methodId,
        std::span<const InterpreterValue> arguments
    ) = 0;
    // Paths traverse stored struct fields beneath the selected property.
    [[nodiscard]] InterpreterValue getProperty(NomadStringView propertyPath) const;
    virtual void setProperty(NomadStringView propertyPath, const InterpreterValue& value) = 0;

protected:
    InterpreterReceiver(const Interpreter& interpreter, const Type& type);
    [[nodiscard]] const Interpreter& getInterpreter() const;
    [[nodiscard]] PropertyId resolveProperty(NomadStringView name) const;

private:
    const Interpreter& m_interpreter;
    const Type& m_type;
};

} // namespace nomad
