// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/CallNode.hpp>
#include <nomad/compiler/FormatStringCompiler.hpp>
#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/script/Member.hpp>

#include <nomad/Nomad.hpp>

namespace nomad {

class ObjectTypeBase;

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
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

    UnaryOperator m_unaryOperator;
    std::unique_ptr<Expression> m_expression;
};

class DynamicFormatExpression final : public Expression {
public:
    DynamicFormatExpression(const Expression* parent, NomadIndex line, NomadIndex column,
        std::unique_ptr<Expression> expression);

    [[nodiscard]] bool isTerminal() const override;

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
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
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

    BinaryOperator m_binaryOperator;
    std::unique_ptr<Expression> m_left;
    std::unique_ptr<Expression> m_right;
};

///////////////////////////////////////////////////////////////////////////////
// Call expressions
///////////////////////////////////////////////////////////////////////////////
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
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    NomadString m_name;
    const Function* m_function = nullptr;
    ArgumentList m_arguments;
};

class NativeMemberExpression final : public Expression {
public:
    NativeMemberExpression(const Expression* parent, NomadIndex line, NomadIndex column, NomadString qualifiedName,
                              NomadString memberName, std::unique_ptr<Expression> receiver, ArgumentList arguments);

    [[nodiscard]] bool isTerminal() const override;
    [[nodiscard]] bool isWritable() const override;
    [[nodiscard]] NativeMemberExpression* asNativeExpression() override;
    [[nodiscard]] const NativeMemberExpression* asNativeExpression() const override;

    [[nodiscard]] bool resolveAssignmentTarget(CompilerContext* context, ScriptFunction* function) override;
    void setAllowVoid(bool allowVoid);
    void compileAssignment(Compiler* compiler, ScriptFunction* function, Expression* expression) override;
    void evaluateAssignment(InterpreterContext& context, const Expression* expression) const override;

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    [[nodiscard]] bool resolveMember(CompilerContext* context, ScriptFunction* function);

    NomadString m_qualifiedName;
    NomadString m_memberName;
    std::unique_ptr<Expression> m_receiver;
    ArgumentList m_arguments;
    NomadId m_memberId = NOMAD_INVALID_ID;
    MemberType m_memberType = MemberType::Unknown;
    const Method* m_method = nullptr;
    bool m_allowVoid = false;
};

class IndexerExpression final : public Expression {
public:
    IndexerExpression(
        const Expression* parent,
        NomadIndex line,
        NomadIndex column,
        std::unique_ptr<Expression> receiver,
        std::vector<std::unique_ptr<Expression>> indexes
    );
    [[nodiscard]] bool isTerminal() const override;
    [[nodiscard]] bool resolveAssignmentTarget(CompilerContext* context, ScriptFunction* function) override;
    void compileAssignment(Compiler* compiler, ScriptFunction* function, Expression* expression) override;
    void evaluateAssignment(InterpreterContext& context, const Expression* expression) const override;

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    [[nodiscard]] bool resolveIndexer(CompilerContext* context, ScriptFunction* function);
    void compileOperands(Compiler* compiler, ScriptFunction* function, Expression* assigned = nullptr);
    void evaluateIndexer(InterpreterContext& context, const Expression* assigned = nullptr) const;

    std::unique_ptr<Expression> m_receiver;
    std::vector<std::unique_ptr<Expression>> m_indexes;
    NomadId m_indexerId = NOMAD_INVALID_ID;
    bool m_canRead = false;
    bool m_canWrite = false;
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
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;

private:
    NomadBoolean m_value;
};

class IntegerLiteral : public PrimaryExpression {
public:
    IntegerLiteral(const Expression* parent, NomadIndex line, NomadIndex column, NomadInteger value);

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;

private:
    NomadInteger m_value;
};

class FloatLiteral : public PrimaryExpression {
public:
    FloatLiteral(const Expression* parent, NomadIndex line, NomadIndex column, NomadFloat value);

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;

private:
    NomadFloat m_value;
};

class ConstructorExpression final : public Expression {
public:
    ConstructorExpression(
        const Expression* parent,
        NomadIndex line,
        NomadIndex column,
        NomadString constructorName,
        const Type* objectType,
        ArgumentList arguments
    );
    [[nodiscard]] bool isTerminal() const override;

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    NomadString m_constructorName;
    const Type* m_objectType;
    ArgumentList m_arguments;
    const Constructor* m_constructor = nullptr;
};

class StringLiteral : public PrimaryExpression {
public:
    StringLiteral(const Expression* parent, NomadIndex line, NomadIndex column, const NomadString& value);

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;

private:
    NomadString m_value;
};

class FormatStringLiteral : public Expression {
public:
    FormatStringLiteral(const Expression* parent, NomadIndex line, NomadIndex column, const NomadString& formatString);

    [[nodiscard]] bool parsePlaceholders(CompilerContext* context, ScriptFunction* function, NomadStringView sourceContent);

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    struct ExpressionPlaceholder {
        NomadId functionId;
        NomadIndex column;
        NomadIndex segmentIndex;
    };

    NomadString m_formatString;
    std::vector<ExpressionPlaceholder> m_expressionPlaceholders;
    std::vector<FormatTemplateSegment> m_segments;
    NomadString m_variableFormat;
};

class ConstantValueExpression : public PrimaryExpression {
public:
    explicit ConstantValueExpression(const Expression* parent, NomadIndex line, NomadIndex column, const NomadString& identifier);

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;

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

    [[nodiscard]] bool isWritable() const override;
    [[nodiscard]] IdentifierExpression* asIdentifierExpression() override;
    [[nodiscard]] const IdentifierExpression* asIdentifierExpression() const override;
    void compileStore(Compiler* compiler) const;
    void evaluateStore(InterpreterContext& context, const RuntimeValue& value) const;

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    bool parseCapture(const Compiler* compiler, ScriptFunction* function);

    NomadString m_identifier;
    IdentifierDefinition m_identifierDefinition = {};
    bool m_writable = false;
};

// Parse an identifier and handle capture.
bool resolveIdentifier(const NomadString& identifier, const Compiler* compiler, ScriptFunction* function, IdentifierDefinition& definition);

///////////////////////////////////////////////////////////////////////////////
// FIle name expressions
///////////////////////////////////////////////////////////////////////////////
class FileNameExpression final : public PrimaryExpression {
public:
    explicit FileNameExpression(const Expression* parent, NomadIndex line, NomadIndex column, NomadString fileName);

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;

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
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;

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
    void onResolve(CompilerContext* context, ScriptFunction* function) override;
    void onCompile(Compiler* compiler, ScriptFunction* function, ExpressionTarget target) override;

  private:
};

} // namespace nomad
