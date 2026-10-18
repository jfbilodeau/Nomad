// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/script/Callable.hpp>

namespace nomad {

class TypedValue;

///////////////////////////////////////////////////////////////////////////////
/// ArgumentList
///////////////////////////////////////////////////////////////////////////////
class ArgumentList {
public:
    explicit ArgumentList();

    void addExpressionArgument(NomadIndex line, NomadIndex column, const Type* type, std::unique_ptr<Expression> expression);
    void addPredicateArgument(NomadIndex line, NomadIndex column, NomadId predicateFunctionId);
    void addCallbackArgument(NomadIndex line, NomadIndex column, const Type* type, NomadId functionId);
    void addEventCallbackArgument(NomadIndex line, NomadIndex column, const Type* type, NomadId eventId, NomadId functionId);
    void addEventDispatchArgument(NomadIndex line, NomadIndex column, const Type* type, const EventDefinition& eventDefinition);
    void addFileNameArgument(NomadIndex line, NomadIndex column, const Type* type, NomadString fileName);
    void addLineNumberArgument(NomadIndex line, NomadIndex column, const Type* type);

    void add(std::unique_ptr<Argument>&& argument);
    void addAll(std::vector<std::unique_ptr<Argument>>&& arguments);

    [[nodiscard]] NomadBoolean resolve(CompilerContext* context, ScriptFunction* function) const;
    void compile(Compiler* compiler, ScriptFunction* function) const;
    // Evaluates arguments in order. The entries of values share the storage owned by evaluatedArguments.
    // Stops at the first argument that reports an error through the context->
    void evaluate(
        InterpreterContext& context,
        std::vector<TypedValue>& evaluatedArguments,
        std::vector<RuntimeValue>& values
    ) const;

    [[nodiscard]]
    NomadIndex getArgumentCount() const;

    // Optional `this` argument. The receiver value must be in the intermediate register when `compile` runs; it
    // is pushed before the other arguments and shares ownership with the intermediate register (it is popped,
    // never freed, with the arguments).
    void setReceiverType(const Type* receiverType);

    [[nodiscard]]
    const Type* getReceiverType() const;

    // Number of stack slots taken by the arguments, including `this`.
    [[nodiscard]]
    NomadIndex getStackCount() const;

    [[nodiscard]]
    std::vector<std::unique_ptr<Argument>>& getArguments();

    [[nodiscard]]
    const std::vector<std::unique_ptr<Argument>>& getArguments() const;

private:
    std::vector<std::unique_ptr<Argument>> m_arguments;
    const Type* m_receiverType = nullptr;
};

// Selects the overload of `name` that matches the already-parsed `arguments`, and assigns the selected parameter
// types to those arguments. Returns an invalid id and fills `error` when no single overload matches.
CallableId selectCallableOverload(
    CompilerContext* context,
    ScriptFunction* function,
    const NomadString& name,
    ArgumentList* arguments,
    NomadString& error
);


///////////////////////////////////////////////////////////////////////////////
/// CallStatementNode
///////////////////////////////////////////////////////////////////////////////
class CallStatementNode : public Statement {
public:
    CallStatementNode(NomadIndex line, NomadIndex column);

    void addArgument(std::unique_ptr<Argument> argument);

    [[nodiscard]] ArgumentList* getArguments();
    [[nodiscard]] const ArgumentList* getArguments() const;

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;

private:
    ArgumentList m_arguments;
};

///////////////////////////////////////////////////////////////////////////////
/// NativeFunctionStatementNode
///////////////////////////////////////////////////////////////////////////////
class NativeFunctionStatementNode : public CallStatementNode {
public:
    NativeFunctionStatementNode(NomadIndex line, NomadIndex column, const NativeFunctionDefinition& definition);

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    NativeFunctionDefinition m_definition;
};

///////////////////////////////////////////////////////////////////////////////
/// FunctionCallStatementNode
///////////////////////////////////////////////////////////////////////////////
class FunctionCallStatementNode : public CallStatementNode {
public:
    FunctionCallStatementNode(NomadIndex line, NomadIndex column, NomadString name);

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    NomadString m_name;
    NomadId m_functionId = NOMAD_INVALID_ID;
};


} // namespace nomad
