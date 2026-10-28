// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/CallNode.hpp>
#include <nomad/compiler/FormatStringCompiler.hpp>
#include <nomad/compiler/Identifier.hpp>
#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/script/Member.hpp>
#include <nomad/script/NativeMember.hpp>

#include <nomad/Nomad.hpp>

namespace nomad {

class ClassType;

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
    [[nodiscard]] std::unique_ptr<TypedValue> getInitialValue(Compiler* compiler) const override;

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;
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
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;
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
    [[nodiscard]] std::unique_ptr<TypedValue> getInitialValue(Compiler* compiler) const override;

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;
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
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    NomadString m_name;
    const Function* m_function = nullptr;
    ArgumentList m_arguments;
};

class NativeMemberExpression final : public Expression {
public:
    NativeMemberExpression(
        const Expression* parent,
        NomadIndex line, 
        NomadIndex column, 
        NomadString qualifiedName,
        NomadString memberName, 
        std::unique_ptr<Expression> receiver, 
        ArgumentList arguments,
        bool hasExplicitArguments = true
    );

    [[nodiscard]] bool isTerminal() const override;
    [[nodiscard]] bool isWritable() const override;
    [[nodiscard]] NativeMemberExpression* asNativeExpression() override;
    [[nodiscard]] const NativeMemberExpression* asNativeExpression() const override;

    [[nodiscard]] bool resolveAssignmentTarget(
        CompilerContext* context, ScriptCallableBase* function, const Type* valueType = nullptr
    ) override;
    void setAllowVoid(bool allowVoid);
    void compileAssignment(Compiler* compiler, ScriptCallableBase* function, Expression* expression) override;
    void evaluateAssignment(InterpreterContext& context, const Expression* expression) const override;

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    [[nodiscard]] bool resolveMember(
        CompilerContext* context, ScriptCallableBase* function, const Type* valueType = nullptr, bool assignment = false
    );

    NomadString m_qualifiedName;
    NomadString m_memberName;
    std::unique_ptr<Expression> m_receiver;
    ArgumentList m_arguments;
    bool m_hasExplicitArguments;
    MemberReference m_memberId;
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
    [[nodiscard]] bool resolveAssignmentTarget(
        CompilerContext* context, ScriptCallableBase* function, const Type* valueType = nullptr
    ) override;
    void compileAssignment(Compiler* compiler, ScriptCallableBase* function, Expression* expression) override;
    void evaluateAssignment(InterpreterContext& context, const Expression* expression) const override;

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    [[nodiscard]] bool resolveIndexer(CompilerContext* context, ScriptCallableBase* function);
    void compileOperands(Compiler* compiler, ScriptCallableBase* function, Expression* assigned = nullptr);
    void evaluateIndexer(InterpreterContext& context, const Expression* assigned = nullptr) const;

    std::unique_ptr<Expression> m_receiver;
    std::vector<std::unique_ptr<Expression>> m_indexes;
    IndexerId m_indexerId;
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
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;

private:
    NomadBoolean m_value;
};

class IntegerLiteral : public PrimaryExpression {
public:
    IntegerLiteral(const Expression* parent, NomadIndex line, NomadIndex column, NomadInteger value);

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;

private:
    NomadInteger m_value;
};

class FloatLiteral : public PrimaryExpression {
public:
    FloatLiteral(const Expression* parent, NomadIndex line, NomadIndex column, NomadFloat value);

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;

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
        const Type* classType,
        ArgumentList arguments
    );
    [[nodiscard]] bool isTerminal() const override;

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    NomadString m_constructorName;
    // Null when the family is parsed without explicit type arguments.
    const Type* m_classType;
    ArgumentList m_arguments;
    const Constructor* m_constructor = nullptr;
};

class StringLiteral : public PrimaryExpression {
public:
    StringLiteral(const Expression* parent, NomadIndex line, NomadIndex column, const NomadString& value);

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;

private:
    NomadString m_value;
};

class FormatStringLiteral : public Expression {
public:
    FormatStringLiteral(const Expression* parent, NomadIndex line, NomadIndex column, const NomadString& formatString);

    [[nodiscard]] bool parsePlaceholders(CompilerContext* context, ScriptCallableBase* function, NomadStringView sourceContent);

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    struct ExpressionPlaceholder {
        CallableId functionId;
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
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    NomadString m_constantName;
    ConstantId m_constantId = ConstantId::INVALID;
};

///////////////////////////////////////////////////////////////////////////////
// Identifier expressions
///////////////////////////////////////////////////////////////////////////////
class IdentifierExpression final : public PrimaryExpression {
public:
    explicit IdentifierExpression(const Expression* parent, NomadIndex line, NomadIndex column, const NomadString& identifier);

    [[nodiscard]] const NomadString& getIdentifier() const;
    [[nodiscard]] bool isWritable() const override;
    [[nodiscard]] IdentifierExpression* asIdentifierExpression() override;
    [[nodiscard]] const IdentifierExpression* asIdentifierExpression() const override;
    [[nodiscard]] std::unique_ptr<TypedValue> getInitialValue(Compiler* compiler) const override;
    void compileStore(Compiler* compiler) const;
    void evaluateStore(InterpreterContext& context, const RuntimeValue& value) const;

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;
    void onEvaluate(InterpreterContext& context) const override;

private:
    bool parseCapture(const Compiler* compiler, ScriptCallableBase* function);

    NomadString m_identifier;
    IdentifierDefinition m_identifierDefinition = {};
    bool m_writable = false;
};

// Parse an identifier and handle capture.
bool resolveIdentifier(const NomadString& identifier, const Compiler* compiler, ScriptCallableBase* function, IdentifierDefinition& definition);

///////////////////////////////////////////////////////////////////////////////
// FIle name expressions
///////////////////////////////////////////////////////////////////////////////
class FileNameExpression final : public PrimaryExpression {
public:
    explicit FileNameExpression(const Expression* parent, NomadIndex line, NomadIndex column, NomadString fileName);

protected:
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;

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
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;

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
    void onResolve(CompilerContext* context, ScriptCallableBase* function) override;
    void onCompile(Compiler* compiler, ScriptCallableBase* function, ExpressionTarget target) override;

  private:
};

} // namespace nomad
