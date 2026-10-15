// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/script/RuntimeValue.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace nomad {

class Type;
class Runtime;
class ScriptFunction;
class VirtualMachine;

class TypedValue {
public:
    TypedValue(const Type* type, const RuntimeValue& value);
    TypedValue(const TypedValue& other);
    TypedValue(TypedValue&& other) noexcept;
    TypedValue& operator=(const TypedValue& other);
    TypedValue& operator=(TypedValue&& other) noexcept;
    ~TypedValue();

    [[nodiscard]] const Type* getType() const;
    [[nodiscard]] const RuntimeValue& getValue() const;

private:
    void release() noexcept;

    const Type* m_type;
    RuntimeValue m_value;
};

struct InterpreterVariableInfo {
    NomadString name;
    const Type* type;
    NomadString value;
};

class InterpreterContext {
public:
    // The caller owns the VM. Interpreter variables persist for this context's lifetime.
    explicit InterpreterContext(VirtualMachine& virtualMachine);
    // Additionally borrows the active script frame, which must outlive this context.
    InterpreterContext(VirtualMachine& virtualMachine, ScriptFunction& function);
    InterpreterContext(const InterpreterContext&) = delete;
    InterpreterContext& operator=(const InterpreterContext&) = delete;
    ~InterpreterContext();

    [[nodiscard]] Runtime* getRuntime() const;
    [[nodiscard]] VirtualMachine* getVirtualMachine() const;
    // Context-owned resolution metadata, never registered with the Runtime.
    [[nodiscard]] const ScriptFunction* getFunction() const;
    [[nodiscard]] bool isFrameBacked() const;
    [[nodiscard]] bool isFrameVariable(NomadId variableId) const;
    void evaluateFunctionCall(NomadId functionId, const std::vector<RuntimeValue>& arguments);
    void evaluateNativeFunctionCall(NomadId functionId, const std::vector<RuntimeValue>& arguments);
    void setParameterResult(NomadId parameterId);

    void setResult(const Type* type, const RuntimeValue& value);
    void setResult(const TypedValue& value);
    void setVoidResult();
    void clearResult();
    [[nodiscard]] const TypedValue* getResult() const;

    void setError(const NomadString& error);
    void setDiagnostic(const Diagnostic& diagnostic);
    [[nodiscard]] const Diagnostic* getDiagnostic() const;
    void clearError();
    [[nodiscard]] NomadBoolean hasError() const;
    [[nodiscard]] const NomadString* getError() const;

    void setVariable(const NomadString& name, const Type* type, const RuntimeValue& value);

    [[nodiscard]] const Type* getVariableType(const NomadString& name) const;
    [[nodiscard]] const Type* getVariableTypeById(NomadId variableId) const;
    [[nodiscard]] const RuntimeValue* getVariableValue(const NomadString& name) const;
    [[nodiscard]] const RuntimeValue* getVariableValueById(NomadId variableId) const;
    [[nodiscard]] std::vector<InterpreterVariableInfo> listVariables() const;

private:
    friend class Interpreter;

    void validateFrame() const;

    Runtime* m_runtime;
    std::unique_ptr<ScriptFunction> m_function;
    VirtualMachine& m_virtualMachine;
    ScriptFunction* m_frameFunction = nullptr;
    NomadIndex m_parameterIndex = 0;
    NomadIndex m_variableIndex = 0;
    NomadIndex m_frameVariableCount = 0;
    std::optional<TypedValue> m_result;
    std::optional<NomadString> m_error;
    std::optional<Diagnostic> m_diagnostic;
    // Indexed by resolution variable id; borrowed frame slots remain empty.
    std::vector<std::unique_ptr<TypedValue>> m_variables;
};

} // namespace nomad
