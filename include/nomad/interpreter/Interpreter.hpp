// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Source.hpp>

#include <nomad/interpreter/InterpreterContext.hpp>
#include <nomad/interpreter/InterpreterObjectReceiver.hpp>
#include <nomad/interpreter/InterpreterStructReceiver.hpp>
#include <nomad/interpreter/InterpreterValue.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/FormatString.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Property.hpp>

#include <memory>
#include <optional>

namespace nomad {

struct StructMethodResult {
    InterpreterValue receiver;
    InterpreterValue result;
};

class Interpreter {
public:
    // The caller owns the VM, which must outlive this interpreter.
    explicit Interpreter(VirtualMachine& machine);
    Interpreter(const Interpreter&) = delete;
    Interpreter& operator=(const Interpreter&) = delete;

    [[nodiscard]] VirtualMachine& getVirtualMachine() const;

    // Retains the object; the interpreter must outlive the receiver.
    [[nodiscard]] std::unique_ptr<InterpreterObjectReceiver> createObjectReceiver(
        const ClassType& type,
        NomadObject* object
    ) const;
    // Borrows live storage; nullptr is valid for a zero-sized struct.
    [[nodiscard]] std::unique_ptr<InterpreterStructReceiver> createStructReceiver(
        const StructType& type,
        RuntimeValue* storage
    ) const;

    [[nodiscard]] InterpreterValue runFunction(
        CallableId functionId,
        std::span<const InterpreterValue> arguments
    ) const;
    [[nodiscard]] InterpreterValue runObjectMethod(
        const InterpreterValue& receiver,
        CallableId methodId,
        std::span<const InterpreterValue> arguments
    ) const;
    [[nodiscard]] InterpreterValue runObjectMethod(
        const ClassType& type,
        NomadObject* object,
        CallableId methodId,
        std::span<const InterpreterValue> arguments
    ) const;
    [[nodiscard]] StructMethodResult runStructMethod(
        const InterpreterValue& receiver,
        CallableId methodId,
        std::span<const InterpreterValue> arguments
    ) const;
    // Mutates borrowed storage directly; the caller keeps it alive for the call.
    [[nodiscard]] InterpreterValue runStructMethod(
        const StructType& type,
        std::span<RuntimeValue> receiverStorage,
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
    // Assigns only the selected field; the caller keeps borrowed storage alive.
    void setProperty(
        const StructType& type,
        std::span<RuntimeValue> receiverStorage,
        PropertyId propertyId,
        const InterpreterValue& value
    ) const;
    void setProperty(
        const StructType& type,
        std::span<RuntimeValue> receiverStorage,
        NomadStringView propertyPath,
        const InterpreterValue& value
    ) const;

    // Source failures are retained as diagnostics; unsuccessful calls have no result.
    [[nodiscard]] NomadBoolean execute(const NomadString& source, InterpreterContext& context) const;
    [[nodiscard]] NomadBoolean execute(const SourceText& source, InterpreterContext& context) const;
    // Placeholder syntax/type errors produce diagnostics; execution exceptions propagate.
    [[nodiscard]] DynamicFormatResult formatString(
        NomadStringView format,
        InterpreterContext& context,
        NomadStringView sourceName,
        NomadIndex line,
        NomadIndex column
    ) const;

private:
    VirtualMachine& m_virtualMachine;

    [[nodiscard]] bool runScriptCallable(
        const ScriptCallableBase& callable,
        const RuntimeValue& receiver = RuntimeValue{}
    ) const;
    void runNativeCallable(
        const NativeFunctionFn& callback,
        const RuntimeValue& receiver = RuntimeValue{}
    ) const;
    void runNativeStructMethod(const NativeMethod& method) const;
    void runStructMethod(const Method& method) const;
    [[nodiscard]] InterpreterValue runObjectMethod(
        const Method& method,
        const Type& receiverType,
        NomadObject* object,
        std::span<const InterpreterValue> arguments
    ) const;

    [[nodiscard]] std::optional<InterpreterValue> evaluateFormatExpression(
        const SourceText& source,
        InterpreterContext& context,
        NomadIndex sourceLine
    ) const;

    [[nodiscard]] InterpreterValue getStoredProperty(
        const InterpreterValue& receiver,
        std::span<const PropertyId> properties
    ) const;
    [[nodiscard]] InterpreterValue setStoredProperty(
        const InterpreterValue& receiver,
        std::span<const PropertyId> properties,
        const InterpreterValue& value
    ) const;
    void setStoredProperty(
        const StructType& type,
        std::span<RuntimeValue> receiverStorage,
        std::span<const PropertyId> properties,
        const InterpreterValue& value
    ) const;
};

} // namespace nomad
