// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/interpreter/InterpreterValue.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/Property.hpp>
#include <nomad/script/RuntimeValue.hpp>
#include <nomad/script/Variable.hpp>

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace nomad {

class Interpreter;
class InterpreterContext;
class InterpreterReceiver;
class InterpreterStorage;
class Type;
class Runtime;
class ScriptCallableBase;
class ScriptFunction;
class VirtualMachine;

class InterpreterEvaluationGuard {
public:
    // Adopts one evaluation entered by InterpreterContext::beginEvaluation().
    explicit InterpreterEvaluationGuard(InterpreterContext& context) noexcept;
    InterpreterEvaluationGuard(const InterpreterEvaluationGuard&) = delete;
    InterpreterEvaluationGuard& operator=(const InterpreterEvaluationGuard&) = delete;
    InterpreterEvaluationGuard(InterpreterEvaluationGuard&&) = delete;
    InterpreterEvaluationGuard& operator=(InterpreterEvaluationGuard&&) = delete;
    ~InterpreterEvaluationGuard();

private:
    InterpreterContext& m_context;
};

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
    // The caller owns the interpreter; it and its VM must outlive this context.
    explicit InterpreterContext(const Interpreter& interpreter);
    InterpreterContext(const Interpreter&& interpreter) = delete;
    // Additionally borrows the active script frame, which must outlive this context.
    InterpreterContext(
        const Interpreter& interpreter,
        const ScriptCallableBase& callable
    );
    InterpreterContext(
        const Interpreter&& interpreter,
        const ScriptCallableBase& callable
    ) = delete;
    InterpreterContext(const InterpreterContext&) = delete;
    InterpreterContext& operator=(const InterpreterContext&) = delete;
    ~InterpreterContext();

    // TODO: Should be Interpeter*
    [[nodiscard]] const Interpreter& getInterpreter() const;
    [[nodiscard]] Runtime* getRuntime() const;
    [[nodiscard]] VirtualMachine* getVirtualMachine() const;
    // Function-only access to context-owned metadata; nullptr for method or constructor frames.
    [[nodiscard]] const ScriptFunction* getFunction() const;
    // Context-owned resolution metadata; validates borrowed frames before access.
    [[nodiscard]] ScriptCallableBase* getCallable();
    [[nodiscard]] const ScriptCallableBase* getCallable() const;
    [[nodiscard]] bool isFrameBacked() const;
    [[nodiscard]] bool isFrameVariable(VariableId variableId) const;
    void setParameterResult(NomadId parameterId);
    void setParameter(NomadId parameterId, const RuntimeValue& value);
    void setParameter(NomadId parameterId, std::span<const RuntimeValue> storage);
    [[nodiscard]] std::span<const RuntimeValue> getParameterStorage(NomadId parameterId) const;
    [[nodiscard]] std::span<RuntimeValue> getMutableParameterStorage(NomadId parameterId);
    // AST receiver access; null for contexts without a receiver. Wrappers borrow the active frame.
    [[nodiscard]] InterpreterReceiver* getReceiver();
    [[nodiscard]] const InterpreterReceiver* getReceiver() const;
    // Borrows the complete struct receiver of an active method/constructor frame.
    [[nodiscard]] std::span<const RuntimeValue> getReceiverStorage() const;
    [[nodiscard]] std::span<RuntimeValue> getMutableReceiverStorage();
    void setReceiverProperty(PropertyId propertyId, const InterpreterValue& value);
    void setReceiverProperty(NomadStringView propertyPath, const InterpreterValue& value);

    void setResult(const Type* type, const RuntimeValue& value);
    void setResult(const Type* type, std::span<const RuntimeValue> storage);
    void setResult(const InterpreterValue& value);
    void setResult(InterpreterValue&& value);
    void setVoidResult();
    void clearResult();
    [[nodiscard]] const InterpreterValue* getResult() const;

    void setError(const NomadString& error);
    void addDiagnostic(const Diagnostic& diagnostic);
    void copyCompilerDiagnostic(
        std::span<const Diagnostic> diagnostics,
        const NomadString& sourceName
    );
    // Includes warnings and errors in reporting order. clearError clears this collection too.
    [[nodiscard]] std::span<const Diagnostic> getDiagnostics() const;
    void clearError();
    [[nodiscard]] NomadBoolean hasError() const;
    [[nodiscard]] NomadBoolean hasWarning() const;
    // First error message, or nullptr. Invalidated by changes to the diagnostics collection.
    [[nodiscard]] const NomadString* getError() const;

    void setVariable(const NomadString& name, const Type* type, const RuntimeValue& value);
    void setVariable(const NomadString& name, const Type* type, std::span<const RuntimeValue> storage);
    // Assigns the selected field in current storage, preserving unrelated RHS changes.
    void setVariableProperty(
        const NomadString& name,
        PropertyId propertyId,
        const InterpreterValue& value
    );
    void setVariableProperty(
        const NomadString& name,
        NomadStringView propertyPath,
        const InterpreterValue& value
    );
    void clearVariableValue(const NomadString& name);

    [[nodiscard]] const Type* getVariableType(const NomadString& name) const;
    [[nodiscard]] const Type* getVariableTypeById(VariableId variableId) const;
    [[nodiscard]] const RuntimeValue* getVariableValue(const NomadString& name) const;
    [[nodiscard]] const RuntimeValue* getVariableValueById(VariableId variableId) const;
    // Empty views also represent missing values; query the variable type to distinguish empty structs.
    [[nodiscard]] std::span<const RuntimeValue> getVariableStorage(const NomadString& name) const;
    [[nodiscard]] std::span<const RuntimeValue> getVariableStorageById(VariableId variableId) const;
    [[nodiscard]] std::span<RuntimeValue> getMutableVariableStorageById(VariableId variableId);
    [[nodiscard]] std::vector<InterpreterVariableInfo> listVariables() const;
    // Nested evaluations retain replaced storage until the outermost exit.
    [[nodiscard]] InterpreterEvaluationGuard beginEvaluation();
    void endEvaluation();

private:
    void validateFrame() const;

    const Interpreter& m_interpreter;
    Runtime* m_runtime;
    std::unique_ptr<ScriptCallableBase> m_function;
    std::unique_ptr<InterpreterReceiver> m_receiver;
    const ScriptCallableBase* m_frameCallable = nullptr;
    NomadIndex m_parameterIndex = 0;
    NomadIndex m_variableIndex = 0;
    NomadIndex m_frameVariableCount = 0;
    std::optional<InterpreterValue> m_result;
    std::vector<Diagnostic> m_diagnostics;
    // Indexed by resolution variable id; borrowed frame slots remain empty.
    std::vector<std::unique_ptr<InterpreterStorage>> m_variables;
    std::vector<std::unique_ptr<InterpreterStorage>> m_retiredStorage;
    NomadIndex m_evaluationDepth = 0;
    LoopControl m_loopControl = LoopControl::None;
};

} // namespace nomad
