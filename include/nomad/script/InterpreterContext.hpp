// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/RuntimeValue.hpp>
#include <nomad/script/TypedValue.hpp>
#include <nomad/script/Variable.hpp>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace nomad {

class Type;
class Runtime;
class ScriptCallableBase;
class ScriptFunction;
class VirtualMachine;

struct InterpreterVariableInfo {
    NomadString name;
    const Type* type;
    NomadString value;
};

enum class LoopControl {
    None = 1,
    Break,
    Continue
};

class InterpreterContext {
public:
    [[nodiscard]] LoopControl getLoopControl() const { return m_loopControl; }
    void setLoopControl(LoopControl control) { m_loopControl = control; }
    // The caller owns the VM. Interpreter variables persist for this context's lifetime.
    explicit InterpreterContext(VirtualMachine& virtualMachine);
    // Additionally borrows the active script frame, which must outlive this context.
    InterpreterContext(VirtualMachine& virtualMachine, const ScriptCallableBase& callable);
    InterpreterContext(const InterpreterContext&) = delete;
    InterpreterContext& operator=(const InterpreterContext&) = delete;
    ~InterpreterContext();

    [[nodiscard]] Runtime* getRuntime() const;
    [[nodiscard]] VirtualMachine* getVirtualMachine() const;
    // Function-only access to context-owned metadata; nullptr for method or constructor frames.
    [[nodiscard]] const ScriptFunction* getFunction() const;
    // Context-owned resolution metadata, never registered with the Runtime.
    [[nodiscard]] const ScriptCallableBase* getCallable() const;
    [[nodiscard]] bool isFrameBacked() const;
    [[nodiscard]] bool isFrameVariable(VariableId variableId) const;
    void evaluateFunctionCall(CallableId functionId, const std::vector<RuntimeValue>& arguments);
    void evaluateNativeFunctionCall(CallableId functionId, const std::vector<RuntimeValue>& arguments);
    // Resolved interpreter calls use complete typed arguments, including zero-slot values.
    void evaluateFunctionCallWithStorage(CallableId functionId, std::span<const TypedValue> arguments);
    void evaluateNativeFunctionCallWithStorage(CallableId functionId, std::span<const TypedValue> arguments);
    void evaluateConstructorCall(CallableId constructorId, std::span<const TypedValue> arguments);
    void setParameterResult(NomadId parameterId);
    void setParameter(NomadId parameterId, const RuntimeValue& value);
    void setParameter(NomadId parameterId, std::span<const RuntimeValue> storage);
    [[nodiscard]] std::span<const RuntimeValue> getParameterStorage(NomadId parameterId) const;
    // Borrows the complete struct receiver of an active method/constructor frame.
    [[nodiscard]] std::span<const RuntimeValue> getReceiverStorage() const;
    void setReceiverProperty(std::span<const PropertyId> properties, const TypedValue& value);

    void setResult(const Type* type, const RuntimeValue& value);
    void setResult(const Type* type, std::span<const RuntimeValue> storage);
    void setResult(const TypedValue& value);
    void setVoidResult();
    void clearResult();
    [[nodiscard]] const TypedValue* getResult() const;

    void setError(const NomadString& error);
    void addDiagnostic(const Diagnostic& diagnostic);
    // Includes warnings and errors in reporting order. clearError clears this collection too.
    [[nodiscard]] std::span<const Diagnostic> getDiagnostics() const;
    void clearError();
    [[nodiscard]] NomadBoolean hasError() const;
    [[nodiscard]] NomadBoolean hasWarning() const;
    // First error message, or nullptr. Invalidated by changes to the diagnostics collection.
    [[nodiscard]] const NomadString* getError() const;

    void setVariable(const NomadString& name, const Type* type, const RuntimeValue& value);
    void setVariable(const NomadString& name, const Type* type, std::span<const RuntimeValue> storage);
    // Copies the current root at write time, preserving unrelated RHS changes.
    void setVariableProperty(
        const NomadString& name,
        std::span<const PropertyId> properties,
        const TypedValue& value
    );
    void clearVariableValue(const NomadString& name);

    [[nodiscard]] const Type* getVariableType(const NomadString& name) const;
    [[nodiscard]] const Type* getVariableTypeById(VariableId variableId) const;
    [[nodiscard]] const RuntimeValue* getVariableValue(const NomadString& name) const;
    [[nodiscard]] const RuntimeValue* getVariableValueById(VariableId variableId) const;
    // Empty views also represent missing values; query the variable type to distinguish empty structs.
    [[nodiscard]] std::span<const RuntimeValue> getVariableStorage(const NomadString& name) const;
    [[nodiscard]] std::span<const RuntimeValue> getVariableStorageById(VariableId variableId) const;
    [[nodiscard]] std::vector<InterpreterVariableInfo> listVariables() const;

private:
    friend class Interpreter;

    void validateFrame() const;

    Runtime* m_runtime;
    std::unique_ptr<ScriptCallableBase> m_function;
    VirtualMachine& m_virtualMachine;
    const ScriptCallableBase* m_frameCallable = nullptr;
    NomadIndex m_parameterIndex = 0;
    NomadIndex m_variableIndex = 0;
    NomadIndex m_frameVariableCount = 0;
    std::optional<TypedValue> m_result;
    LoopControl m_loopControl = LoopControl::None;
    std::vector<Diagnostic> m_diagnostics;
    // Indexed by resolution variable id; borrowed frame slots remain empty.
    std::vector<std::unique_ptr<TypedValue>> m_variables;
};

} // namespace nomad
