// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/compiler/Identifier.hpp>
#include <nomad/compiler/Operators.hpp>

#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/FormatString.hpp>

#include <memory>
#include <vector>

namespace nomad {

// Forward declarations
class Argument;
class AstNode;
class Compiler;
class CompilerContext;
class FunStatementNode;  // Defined in StatementParsers.hpp
class Interpreter;
class TypedValue;
class Runtime;
class FunctionBuilder;

// Forward declarations in file
class CallNativeFunctionExpression;
class CallStatementNode;
class Expression;
class Statement;
class StatementList;

///////////////////////////////////////////////////////////////////////////////
// AST exception
///////////////////////////////////////////////////////////////////////////////
class AstException : public NomadException {
public:
    explicit AstException(const NomadString& message, NomadIndex line, NomadIndex column);
    explicit AstException(const NomadString& message, NomadIndex startLine, NomadIndex startColumn, NomadIndex endLine, NomadIndex endColumn);

    [[nodiscard]] NomadIndex getLine() const;
    [[nodiscard]] NomadIndex getColumn() const;

private:
    NomadIndex m_line;
    NomadIndex m_column;
};

///////////////////////////////////////////////////////////////////////////////
// Abstract syntax tree
///////////////////////////////////////////////////////////////////////////////
class AstNode {
public:
    AstNode(NomadIndex line, NomadIndex column);
    AstNode(const AstNode&) = delete;
    virtual ~AstNode() = default;

    [[nodiscard]] NomadIndex getLine() const;
    [[nodiscard]] NomadIndex getColumn() const;
    [[nodiscard]] NomadIndex getEndLine() const;
    [[nodiscard]] NomadIndex getEndColumn() const;

    void setEndSpan(NomadIndex endLine, NomadIndex endColumn);

protected:
    [[noreturn]] void raiseException(const NomadString& message) const;
    void reportError(CompilerContext* context, const Function* function, const NomadString& message) const;
    void reportWarning(CompilerContext* context, const Function* function, const NomadString& message) const;

private:
    struct SourceSpan {
        NomadIndex startLine = 0;
        NomadIndex startColumn = 0;
        NomadIndex endLine = 0;
        NomadIndex endColumn = 0;
    };

    SourceSpan m_span;
};

///////////////////////////////////////////////////////////////////////////////
// Expressions
///////////////////////////////////////////////////////////////////////////////
enum class ExpressionTarget {
    Result,
    Intermediate,
    Stack
};

class Expression : public AstNode {
public:
    Expression(const Expression* parent, NomadIndex line, NomadIndex column, const Type* type = nullptr);
    ~Expression() override;
    // Determines the expression type (and value when it can be folded). Returns false if errors were reported.
    [[nodiscard]] NomadBoolean resolve(CompilerContext* context, Function* function);
    // The expression must have been resolved first.
    void compile(Compiler* compiler, Function* function, ExpressionTarget target);
    // Can the expression lend a pointer to a string it does not own (`$stringref`) instead of producing a copy?
    // Only literals/constants, function variables and parameters can: their storage outlives the borrowing operation.
    [[nodiscard]] virtual bool canBorrow() const;
    // Load a borrowed string pointer into `target`. Only valid when `canBorrow()` is true.
    void compileBorrowed(Compiler* compiler, Function* function, ExpressionTarget target);
    // The expression must have been resolved first.
    void evaluate(Interpreter& context) const;

    [[nodiscard]] const Type* getType() const;
    [[nodiscard]] const RuntimeValue& getValue() const;
    [[nodiscard]] bool hasType() const;
    [[nodiscard]] bool hasValue() const;
    [[nodiscard]] bool isParsed() const;
    // Is the expression terminal (no further computation necessary) or compound (needs further evaluation)
    // ie: the r/i registers will be used if false.
    [[nodiscard]]
    virtual bool isTerminal() const;

    // Get the statement this expression belongs to:
    [[nodiscard]]
    const Statement* getStatement() const;

protected:
    void setResolved(const RuntimeValue& value, const Type* type);
    void setType(const Type* type);
    void setValue(const RuntimeValue& value);

    virtual void onResolve(CompilerContext* context, Function* function) = 0;
    virtual void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) = 0;
    virtual void onCompileBorrowed(Compiler* compiler, Function* function, ExpressionTarget target);
    virtual void onEvaluate(Interpreter& context) const;
    [[nodiscard]]
    virtual const Statement* onGetStatement() const;

private:
    const Expression* m_parent;
    RuntimeValue m_value;
    const Type* m_type = nullptr;
    bool m_hasValue = false;
};

///////////////////////////////////////////////////////////////////////////////
// Statements
///////////////////////////////////////////////////////////////////////////////

class Statement : public AstNode {
public:
    Statement(NomadIndex line, NomadIndex column);
    ~Statement() override = default;

    // Resolves identifiers and expression types. Returns false if errors were reported.
    [[nodiscard]] NomadBoolean resolve(CompilerContext* context, Function* function);
    void compile(Compiler* compiler, Function* function);
    void evaluate(Interpreter& context) const;

    // True when every execution path through this statement ends with a `return`.
    [[nodiscard]] virtual NomadBoolean alwaysReturns() const;
    // Declarations (`fun`, `const`, `event`, `on`) emit no code, so they are never unreachable.
    [[nodiscard]] virtual NomadBoolean isDeclaration() const;
    // Reports statements that follow a `return` in any statement list nested in this statement.
    virtual void checkReachability(CompilerContext* context, const Function* function) const;

protected:
    virtual void onResolve(CompilerContext* context, Function* function);
    virtual void onCompile(Compiler* compiler, Function* function);
    virtual void onEvaluate(Interpreter& context) const;
};

class NullStatement : public Statement {
public:
    NullStatement(NomadIndex line, NomadIndex column);

protected:
    void onCompile(Compiler* compiler, Function* function) override;
    void onEvaluate(Interpreter& context) const override;
};

class AssignmentStatement : public Statement {
public:
    AssignmentStatement(NomadIndex line, NomadIndex column, NomadString identifier, std::unique_ptr<Expression> expression);

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function) override;
    void onEvaluate(Interpreter& context) const override;

private:
    NomadString m_identifier;
    IdentifierDefinition m_identifierDefinition;
    std::unique_ptr<Expression> m_expression;
};

class ExpressionStatement final : public Statement {
public:
    ExpressionStatement(NomadIndex line, NomadIndex column, std::unique_ptr<Expression> expression);

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function) override;
    void onEvaluate(Interpreter& context) const override;

private:
    std::unique_ptr<Expression> m_expression;
};

///////////////////////////////////////////////////////////////////////////////
// Statement list (Not an AST node)
///////////////////////////////////////////////////////////////////////////////
class StatementList {
public:
    StatementList() = default;
    StatementList(StatementList&&) = default;

    StatementList& operator=(StatementList&&) = default;

    [[nodiscard]] NomadBoolean resolve(CompilerContext* context, Function* function) const;
    void compile(Compiler* compiler, Function* function) const;
    void evaluate(Interpreter& context) const;

    void addStatement(std::unique_ptr<Statement> statement);

    [[nodiscard]] NomadIndex getStatementCount() const;
    [[nodiscard]] NomadIndex isEmpty() const;
    // True when execution cannot reach the end of the list without executing a `return`.
    [[nodiscard]] NomadBoolean alwaysReturns() const;
    // Reports the first statement that follows one that always returns, then checks nested statement lists.
    void checkReachability(CompilerContext* context, const Function* function) const;

private:
    std::vector<std::unique_ptr<Statement>> m_statements;
};

///////////////////////////////////////////////////////////////////////////////
// Function and program
///////////////////////////////////////////////////////////////////////////////

class FunctionNode : public AstNode {
public:
    FunctionNode(NomadIndex line, NomadIndex row);

    [[nodiscard]] NomadBoolean resolve(CompilerContext* context, Function* function) const;
    void compile(Compiler* compiler, Function* function) const;
    [[nodiscard]] NomadBoolean alwaysReturns() const;
    void checkReachability(CompilerContext* context, const Function* function) const;

    void addStatement(std::unique_ptr<Statement> statement);
    [[nodiscard]] StatementList* getStatements();

private:
    StatementList m_statements;
};

///////////////////////////////////////////////////////////////////////////////
// Declaration nodes
///////////////////////////////////////////////////////////////////////////////

class ParameterDeclaration : public AstNode {
public:
    ParameterDeclaration(NomadIndex line, NomadIndex column, NomadString name, const Type* type);

    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] const Type* getType() const;

private:
    NomadString m_name;
    const Type* m_type = nullptr;
};

class FunctionDeclaration : public Statement {
public:
    FunctionDeclaration(NomadIndex line, NomadIndex column, NomadString name);
    void setFunctionId(NomadId id);
    [[nodiscard]] NomadId getFunctionId() const;

    void addParam(std::unique_ptr<ParameterDeclaration> param);
    void setBody(std::unique_ptr<FunctionNode> body);

    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] const std::vector<std::unique_ptr<ParameterDeclaration>>& getParams() const;
    [[nodiscard]] FunctionNode* getBody() const;
    [[nodiscard]] NomadBoolean isDeclaration() const override;

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function) override;

private:
    NomadString m_name;
    std::vector<std::unique_ptr<ParameterDeclaration>> m_params;
    std::unique_ptr<FunctionNode> m_body;
    const Type* m_returnType = nullptr;
    NomadId m_functionId = NOMAD_INVALID_ID;
};

class EventDeclaration : public Statement {
public:
    EventDeclaration(NomadIndex line, NomadIndex column, NomadString name);

    void addParam(std::unique_ptr<ParameterDeclaration> param);

    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] const std::vector<std::unique_ptr<ParameterDeclaration>>& getParams() const;
    [[nodiscard]] NomadBoolean isDeclaration() const override;

protected:
    void onResolve(CompilerContext* context, Function* function) override;

private:
    NomadString m_name;
    std::vector<std::unique_ptr<ParameterDeclaration>> m_params;
};

class ConstDeclaration : public Statement {
public:
    ConstDeclaration(NomadIndex line, NomadIndex column, NomadString name, std::unique_ptr<Expression> initializer);

    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] const Expression* getInitializer() const;
    [[nodiscard]] NomadBoolean isDeclaration() const override;

protected:
    void onResolve(CompilerContext* context, Function* function) override;

private:
    NomadString m_name;
    std::unique_ptr<Expression> m_initializer;
    const Type* m_type = nullptr;
};

class EventHandlerDeclaration : public Statement {
public:
    EventHandlerDeclaration(NomadIndex line, NomadIndex column, NomadString eventName, std::unique_ptr<FunctionDeclaration> handler);

    [[nodiscard]] const NomadString& getEventName() const;
    [[nodiscard]] const FunctionDeclaration* getHandler() const;
    [[nodiscard]] NomadBoolean isDeclaration() const override;

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function) override;

private:
    NomadString m_eventName;
    std::unique_ptr<FunctionDeclaration> m_handler;
};

} // nomad
