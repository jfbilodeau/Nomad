// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Source.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/InterpreterContext.hpp>
#include <nomad/script/InterpreterValue.hpp>
#include <nomad/script/Property.hpp>

namespace nomad {

class Interpreter {
public:
    // The caller owns the VM, which must outlive this interpreter.
    explicit Interpreter(VirtualMachine& machine);
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    [[nodiscard]] VirtualMachine& getVirtualMachine() const;

    [[nodiscard]] InterpreterValue callFunction(
        CallableId functionId,
        std::span<const InterpreterValue> arguments
    ) const;
    [[nodiscard]] InterpreterValue callMethod(
        const InterpreterValue& receiver,
        CallableId methodId,
        std::span<const InterpreterValue> arguments
    ) const;
    [[nodiscard]] InterpreterValue construct(
        CallableId constructorId,
        std::span<const InterpreterValue> arguments
    ) const;

    [[nodiscard]] InterpreterValue getProperty(
        const InterpreterValue& receiver,
        PropertyId propertyId
    ) const;
    [[nodiscard]] InterpreterValue getProperty(
        const InterpreterValue& receiver,
        NomadStringView propertyPath
    ) const;
    // Returns a replacement struct without modifying either input.
    [[nodiscard]] InterpreterValue setProperty(
        const InterpreterValue& receiver,
        PropertyId propertyId,
        const InterpreterValue& value
    ) const;
    [[nodiscard]] InterpreterValue setProperty(
        const InterpreterValue& receiver,
        NomadStringView propertyPath,
        const InterpreterValue& value
    ) const;

    [[nodiscard]] NomadBoolean execute(const NomadString& source, InterpreterContext& context) const;
    [[nodiscard]] NomadBoolean execute(const SourceText& source, InterpreterContext& context) const;
    // Requires one non-void expression. Syntax/type errors are retained; execution faults propagate.
    [[nodiscard]] bool evaluateExpression(const NomadString& source, InterpreterContext& context) const;
    [[nodiscard]] bool evaluateExpression(
        const SourceText& source,
        InterpreterContext& context,
        NomadIndex sourceLine = 1
    ) const;

private:
    VirtualMachine& m_virtualMachine;

    [[nodiscard]] InterpreterValue getStoredProperty(
        const InterpreterValue& receiver,
        std::span<const PropertyId> properties
    ) const;
    [[nodiscard]] InterpreterValue setStoredProperty(
        const InterpreterValue& receiver,
        std::span<const PropertyId> properties,
        const InterpreterValue& value
    ) const;
};

} // namespace nomad
