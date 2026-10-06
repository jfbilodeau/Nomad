// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/CallNode.hpp>
#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/Nomad.hpp>

namespace nomad {

///////////////////////////////////////////////////////////////////////////////
// Unary expressions
///////////////////////////////////////////////////////////////////////////////
class UnaryExpression : public Expression {
public:
    explicit UnaryExpression(
        const Expression* parent,
        NomadIndex line,
        NomadIndex column,
        UnaryOperator unaryOperator,
        std::unique_ptr<Expression> expression
    );

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;
    void onEvaluate(Interpreter& context) const override;

    UnaryOperator m_unaryOperator;
    std::unique_ptr<Expression> m_expression;
};

///////////////////////////////////////////////////////////////////////////////
// Binary expressions
///////////////////////////////////////////////////////////////////////////////
class BinaryExpression : public Expression {
public:
    BinaryExpression(const Expression* parent, NomadIndex line, NomadIndex column, BinaryOperator binaryOperator, std::unique_ptr<Expression> left, std::unique_ptr<Expression> right);

    [[nodiscard]]
    bool isTerminal() const override;

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;
    void onEvaluate(Interpreter& context) const override;

    BinaryOperator m_binaryOperator;
    std::unique_ptr<Expression> m_left;
    std::unique_ptr<Expression> m_right;
};

///////////////////////////////////////////////////////////////////////////////
// Call expressions
///////////////////////////////////////////////////////////////////////////////
class CallNativeFunctionExpression : public Expression {
public:
    explicit CallNativeFunctionExpression(const Expression* parent, NomadIndex line, NomadIndex column, NomadString name);

    [[nodiscard]]
    bool isTerminal() const override;

    void addArgument(std::unique_ptr<Argument> argument);

    [[nodiscard]] ArgumentList* getArguments();
    [[nodiscard]] const ArgumentList* getArguments() const;

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;
    void onEvaluate(Interpreter& context) const override;

private:
    NomadString m_name;
    NomadId m_nativeFunctionId = NOMAD_INVALID_ID;
    ArgumentList m_arguments;
};

class FunctionCallExpression final : public Expression {
public:
    explicit FunctionCallExpression(const Expression* parent, NomadIndex line, NomadIndex column, NomadString name);

    [[nodiscard]]
    bool isTerminal() const override;

    void addArgument(std::unique_ptr<Argument> argument);

    [[nodiscard]] ArgumentList* getArguments();
    [[nodiscard]] const ArgumentList* getArguments() const;
    [[nodiscard]] NomadIndex getArgumentCount() const;

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;
    void onEvaluate(Interpreter& context) const override;

private:
    NomadString m_name;
    NomadId m_functionId = NOMAD_INVALID_ID;
    ArgumentList m_arguments;
};

///////////////////////////////////////////////////////////////////////////////
// Primary expressions
///////////////////////////////////////////////////////////////////////////////
class PrimaryExpression : public Expression {
public:
    PrimaryExpression(const Expression* parent, NomadIndex line, NomadIndex column);
    ~PrimaryExpression() override = default;
};

class BooleanLiteral : public PrimaryExpression {
public:
    BooleanLiteral(
        const Expression* parent,
        NomadIndex line,
        NomadIndex column,
        NomadBoolean m_value
    );

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;

private:
    NomadBoolean m_value;
};

class IntegerLiteral : public PrimaryExpression {
public:
    IntegerLiteral(const Expression* parent, NomadIndex line, NomadIndex column, NomadInteger value);

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;

private:
    NomadInteger m_value;
};

class FloatLiteral : public PrimaryExpression {
public:
    FloatLiteral(const Expression* parent, NomadIndex line, NomadIndex column, NomadFloat value);

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;

private:
    NomadFloat m_value;
};

class StringLiteral : public PrimaryExpression {
public:
    StringLiteral(const Expression* parent, NomadIndex line, NomadIndex column, const NomadString& value);

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;

private:
    NomadString m_value;
};

class FormatStringLiteral : public Expression {
public:
    FormatStringLiteral(const Expression* parent, NomadIndex line, NomadIndex column, const NomadString& formatString);

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;

private:
    NomadString m_formatString;
};

class ConstantValueExpression : public PrimaryExpression {
public:
    explicit ConstantValueExpression(const Expression* parent, NomadIndex line, NomadIndex column, const NomadString& identifier);

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;

private:
    NomadString m_constantName;
    NomadId m_constantId = NOMAD_INVALID_ID;
};

///////////////////////////////////////////////////////////////////////////////
// Identifier expressions
///////////////////////////////////////////////////////////////////////////////
class IdentifierExpression final : public PrimaryExpression {
public:
    explicit IdentifierExpression(const Expression* parent, NomadIndex line, NomadIndex column, const NomadString& identifier);

    [[nodiscard]] bool canBorrow() const override;

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;
    void onCompileBorrowed(Compiler* compiler, Function* function, ExpressionTarget target) override;
    void onEvaluate(Interpreter& context) const override;

private:
    bool parseCapture(const Compiler* compiler, Function* function);

    NomadString m_identifier;
    IdentifierDefinition m_identifierDefinition = {};
};

// Parse an identifier and handle capture.
bool resolveIdentifier(const NomadString& identifier, const Compiler* compiler, Function* function, IdentifierDefinition& definition);

///////////////////////////////////////////////////////////////////////////////
// FIle name expressions
///////////////////////////////////////////////////////////////////////////////
class FileNameExpression final : public PrimaryExpression {
public:
    explicit FileNameExpression(const Expression* parent, NomadIndex line, NomadIndex column, NomadString fileName);

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;

private:
    NomadString m_fileName;
};

///////////////////////////////////////////////////////////////////////////////
// Function name expressions
///////////////////////////////////////////////////////////////////////////////
class FunctionNameExpression final : public PrimaryExpression {
public:
    explicit FunctionNameExpression(const Expression* parent, NomadIndex line, NomadIndex column, NomadString  functionName);

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;

private:
    NomadString m_functionName;
};

///////////////////////////////////////////////////////////////////////////////
// Function line number expressions
///////////////////////////////////////////////////////////////////////////////
class LineNumberExpression final : public PrimaryExpression {
public:
    explicit LineNumberExpression(const Expression* parent, NomadIndex line, NomadIndex column);

protected:
    void onResolve(CompilerContext* context, Function* function) override;
    void onCompile(Compiler* compiler, Function* function, ExpressionTarget target) override;

private:
};

} // namespace nomad
