// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/script/Event.hpp>

namespace nomad {

struct EventDefinition;

///////////////////////////////////////////////////////////////////////////////
/// Argument
///////////////////////////////////////////////////////////////////////////////
class Argument : public AstNode {
public:
    Argument(NomadIndex line, NomadIndex column, const Type* type);
    ~Argument() override = default;

    [[nodiscard]]
    const Type* getType() const;

    // Overload resolution assigns the parameter type only once the overload has been selected.
    void setType(const Type* type);

    // Resolves the argument's own expression without checking it against the parameter type, and returns the type
    // that expression yields. Returns `nullptr` for arguments whose value does not depend on the parameter type, and
    // for expressions that failed to resolve. Overload resolution uses this to learn the argument types before the
    // parameter types are known.
    [[nodiscard]] virtual const Type* resolveOverloadType(CompilerContext* context, ScriptCallableBase* function);

    [[nodiscard]]
    virtual NomadIndex getStackValueCount() const { return 1; }

    [[nodiscard]] TypedValue evaluate(InterpreterContext& context) const;
    // Resolves the argument's expression (if any). Returns false if errors were reported.
    [[nodiscard]] NomadBoolean resolve(CompilerContext* context, ScriptCallableBase* function);
    void generateCode(Compiler* compiler, ScriptCallableBase* function);

protected:
    virtual void onResolve(CompilerContext* context, ScriptCallableBase* function);
    virtual void onCompile(Compiler* compiler, ScriptCallableBase* function) = 0;
    [[nodiscard]] virtual TypedValue onEvaluate(InterpreterContext& context) const;

private:
    const Type* m_type = nullptr;
};

// Argument factories
std::unique_ptr<Argument> createExpressionArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    std::unique_ptr<Expression> expression
);
std::unique_ptr<Argument> createPredicateArgument(NomadIndex line, NomadIndex column, CallableId predicateFunctionId);
std::unique_ptr<Argument> createCallbackArgument(NomadIndex line, NomadIndex column, const Type* type, CallableId functionId);
std::unique_ptr<Argument> createEventCallbackArgument(NomadIndex line, NomadIndex column, const Type* type, EventId eventId, CallableId functionId);
std::unique_ptr<Argument> createEventDispatchArgument(NomadIndex line, NomadIndex column, const Type* type, const EventDefinition& eventDefinition);
std::unique_ptr<Argument> createFileNameArgument(NomadIndex line, NomadIndex column, const Type* type, NomadString fileName);
std::unique_ptr<Argument> createFunctionNameArgument(NomadIndex line, NomadIndex column, const Type* type, NomadString functionName);
std::unique_ptr<Argument> createLineNumberArgument(NomadIndex line, NomadIndex column, const Type* type);

} // namespace nomad
