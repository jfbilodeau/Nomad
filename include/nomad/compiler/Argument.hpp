// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/SyntaxTree.hpp>

namespace nomad {

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
    [[nodiscard]] virtual const Type* resolveOverloadType(CompilerContext* context, Function* function);

    // Type of the value actually pushed on the stack; decides whether the caller frees it after the call.
    // Differs from `getType()` when a `$stringref` parameter receives an owned temporary, or when a borrowable string
    // is lent to a read-only function parameter.
    [[nodiscard]]
    virtual const Type* getPushedType() const;

    [[nodiscard]]
    virtual NomadIndex getStackValueCount() const { return 1; }

    [[nodiscard]] TypedValue evaluate(Interpreter& context) const;
    // Resolves the argument's expression (if any). Returns false if errors were reported.
    [[nodiscard]] NomadBoolean resolve(CompilerContext* context, Function* function);
    void generateCode(Compiler* compiler, Function* function);

protected:
    virtual void onResolve(CompilerContext* context, Function* function);
    virtual void onCompile(Compiler* compiler, Function* function) = 0;
    [[nodiscard]] virtual TypedValue onEvaluate(Interpreter& context) const;

private:
    const Type* m_type = nullptr;
};

// How the callee uses a parameter. A read-only parameter cannot be changed by the callee, so the caller may lend a
// borrowable string (literal, local or parameter) instead of pushing a copy.
enum class ParameterAccess {
    Default = 1,
    ReadOnly,
};

// Argument factories
std::unique_ptr<Argument> createExpressionArgument(
    NomadIndex line,
    NomadIndex column,
    const Type* type,
    std::unique_ptr<Expression> expression,
    ParameterAccess parameterAccess = ParameterAccess::Default
);
std::unique_ptr<Argument> createPredicateArgument(NomadIndex line, NomadIndex column, NomadId predicateFunctionId);
std::unique_ptr<Argument> createCallbackArgument(NomadIndex line, NomadIndex column, const Type* type, NomadId functionId);
std::unique_ptr<Argument> createEventCallbackArgument(NomadIndex line, NomadIndex column, const Type* type, NomadId eventId, NomadId functionId);
std::unique_ptr<Argument> createEventDispatchArgument(NomadIndex line, NomadIndex column, const Type* type, const EventDefinition& eventDefinition);
std::unique_ptr<Argument> createFileNameArgument(NomadIndex line, NomadIndex column, const Type* type, NomadString fileName);
std::unique_ptr<Argument> createFunctionNameArgument(NomadIndex line, NomadIndex column, const Type* type, NomadString functionName);
std::unique_ptr<Argument> createLineNumberArgument(NomadIndex line, NomadIndex column, const Type* type);

} // namespace nomad
