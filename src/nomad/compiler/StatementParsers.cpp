// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/StatementParsers.hpp>

#include <nomad/Nomad.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>
#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/String.hpp>

#include <algorithm>

namespace nomad {

NomadString generateFunFunctionName(CompilerContext* context, Function* function, NomadIndex line) {
    // Generate function name
    NomadString testFunctionName;
    auto count = 0;
    auto testId = NOMAD_INVALID_ID;

    do {
        testFunctionName = function->getName() + "@" + toString(line) + "[" + toString(count) + "]";
        testId = context->getRuntime()->getFunctionId(testFunctionName);
        count++;

        if (count > 1000) {
            // Hopefully this will never happen...
            context->getCompiler()->reportInternalError("could not generate `fun` function name");
        }
    } while (testId != NOMAD_INVALID_ID);

    return testFunctionName;
}

FunStatementNode::FunStatementNode(NomadIndex line, NomadIndex column, Function* function, std::unique_ptr<Expression> /*ptr*/):
    Statement(line, column),
    m_function(function)
{}

void FunStatementNode::onCompile(Compiler* /*compiler*/, Function* /*function*/) {
    // `fun` will be build along other functions
}

void FunStatementNode::addParameter(const NomadString& parameter_name) {
    m_parameters.push_back(parameter_name);
}

NomadIndex FunStatementNode::getParameterCount() const {
    return m_parameters.size();
}

Function* FunStatementNode::getFunction() const {
    return m_function;
}

const NomadString& FunStatementNode::getFunctionName() const {
    return m_function->getName();
}

StatementList& FunStatementNode::getBody() {
    return m_body;
}

bool preParseFunStatement(CompilerContext* context, Function* function, Tokenizer* tokens) {
    NomadString funName;

    if (!tokens->nextIdentifier(funName)) {
        return false;
    }

    // Make sure name is not already in use.
    auto identifierType = parser::getIdentifierType(context->getCompiler(), funName);

    if (identifierType == IdentifierType::ContextVariable) {
        // See if the name of the context variable is used.
        const auto contextId = context->getRuntime()->getVariableContextIdByPrefix(funName);

        if (contextId == NOMAD_INVALID_ID) {
            context->getCompiler()->reportInternalError("Unknown variable context for '" + funName + "'");
        }

        const auto variableId = context->getRuntime()->getContextVariableId(funName);

        if (variableId != NOMAD_INVALID_ID) {
            log::warning("Function name '" + funName + "' is already used as a context variable name");
        }
    } else if (identifierType != IdentifierType::Unknown) {
        context->reportError("Function name '" + funName + "' is already used", tokens);

        return false;
    }

    // Generate `fun`
    auto funId = context->getCompiler()->registerFunctionSource(
        funName,
        function->getPath(),
        function->getSource()
    );

    auto fun = context->getRuntime()->getFunction(funId);

    // Parse parameters.
    return preParseParamsStatement(context, fun, tokens);
}

bool parseCallbackFunParameters(CompilerContext* context, Function* function, Tokenizer* tokens, const FunctionType* callbackType) {
    for (auto i = NomadIndex{0}; i < callbackType->getParameterCount(); ++i) {
        NomadString parameterName;

        if (!tokens->nextIdentifier(parameterName)) {
            return false;
        }

        const auto parameterType = callbackType->getParameterType(toNomadId(i));

        if (function->getParameterId(parameterName) != NOMAD_INVALID_ID) {
            context->reportError("Parameter '" + parameterName + "' is already defined", tokens);

            return false;
        }

        function->addParameter(parameterName, parameterType);
    }

    return true;
}

std::unique_ptr<FunctionNode> parseFunBody(CompilerContext* context, Function* function, Tokenizer* tokens) {
    auto fun = std::make_unique<FunctionNode>(tokens->getLineIndex(), 0);

    const auto valid = parser::parseBlock(context, function, tokens, {"end"}, fun->getStatements());

    if (tokens->isEndOfFile()) {
        return nullptr;
    }

    fun->setEndSpan(tokens->getLineIndex(), tokens->getColumnIndex());

    // Consume `end` token
    tokens->next();

    if (!valid) {
        return nullptr;
    }

    return fun;
}

std::unique_ptr<FunctionNode> parseThenBody(CompilerContext* context, Function* function, Tokenizer* tokens) {
    const auto errorCount = context->getErrorCount();

    auto thenBody = std::make_unique<FunctionNode>(tokens->getLineIndex(), 0);

    while (!tokens->isEndOfFile()) {
        auto statement = parser::parseLine(context, function, tokens);
        thenBody->addStatement(std::move(statement));

        if (!tokens->nextLine()) {
            break;
        }
    }

    thenBody->setEndSpan(tokens->getLineIndex(), 0);

    if (context->getErrorCount() != errorCount) {
        return nullptr;
    }

    return thenBody;
}

std::unique_ptr<Statement> parseFunStatement(CompilerContext* context, Function* /*function*/, Tokenizer* tokens) {
    NomadString funName;

    if (!tokens->is(TokenType::Identifier)) {
        context->reportError("Identifier expected", tokens);

        return nullptr;
    }

    funName = tokens->next().textValue;

    // Fun is already registered by pre_parse_fun_statement
    tokens->nextLine();

    auto funId =  context->getRuntime()->getFunctionId(funName);
    auto funFunction = context->getRuntime()->getFunction(funId);

    if (funFunction == nullptr) {
        // Pre-parsing failed to register the function and already reported why.
        context->reportError("Failed to get function '" + funName + "'", tokens);

        return nullptr;
    }

    auto funBody = parseFunBody(context, funFunction, tokens);

    if (funBody == nullptr) {
        return nullptr;
    }

    context->getCompiler()->setFunctionNode(funId, std::move(funBody));

    // Create a FunctionDeclaration node for the AST so the declaration is visible in the parsed tree.
    auto functionDecl = std::make_unique<FunctionDeclaration>(
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        funName
    );

    // Link AST node to the runtime function id for later passes/tooling
    functionDecl->setFunctionId(funId);

    return functionDecl;
}

class AssertStatement : public Statement {
public:
    AssertStatement(NomadIndex row, NomadIndex col, NomadString line, std::unique_ptr<Expression> expression) :
        Statement(row, col),
        m_line(std::move(line)),
        m_expression(std::move(expression))
    {}

protected:
    void onResolve(CompilerContext* context, Function* function) override {
        if (!m_expression->resolve(context, function)) {
            return;
        }

        if (m_expression->getType() != context->getRuntime()->getBooleanType()) {
            reportError(context, function, "Assert expression must be a boolean expression");
        }
    }

    void onCompile(Compiler* compiler, Function* function) override {
        auto assert_message =
            "Assertion failed: " +
            function->getName() +
            "[" +
            toString(getLine()) +
            "]: " +
            m_line;

        auto assertMessageId = compiler->getRuntime()->registerString(assert_message);

        m_expression->compile(compiler, function, ExpressionTarget::Result);

        compiler->addOpCode(OpCodes::op_id_load_i);
        compiler->addId(assertMessageId);

        compiler->addOpCode(OpCodes::op_assert);
    }

private:
    NomadString m_line;
    std::unique_ptr<Expression> m_expression;
};

std::unique_ptr<Statement> parseAssertStatement(CompilerContext* context, Function* function, Tokenizer* tokens) {
    auto line = stringTrimCopy(tokens->getLine());

    auto expression = parser::parseExpression(context, function, tokens);

    if (expression == nullptr) {
        return nullptr;
    }

    return std::make_unique<AssertStatement>(
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        std::move(line),
        std::move(expression)
    );
}

class ConstStatement : public Statement {
public:
    ConstStatement(NomadIndex row, NomadIndex col, NomadString name, const Type* type, const RuntimeValue& value) :
        Statement(row, col),
        m_name(std::move(name)),
        m_type(type),
        m_value(value)
    {}

protected:
    void onCompile(Compiler* /*compiler*/, Function* /*function*/) override {
        // Nothing to do...
    }

private:
    NomadString m_name;
    const Type* m_type;
    RuntimeValue m_value;
};

struct ConstantValue {
    ~ConstantValue() {
        if (type != nullptr) {
            type->freeValue(value);
        }
    }

    RuntimeValue value;
    const Type* type = nullptr;
};

// The constant expression parsers below report errors to the tokenizer's context and return false on error.
bool parseConstantExpression(CompilerContext* context, Tokenizer* tokens, ConstantValue& constant);

bool parseConstantPrimary(CompilerContext* context, Tokenizer* tokens, ConstantValue& constant) {
    if (tokens->endOfLine()) {
        context->reportError("Unexpected end of line in constant expression", tokens);

        return false;
    }

    auto& token = tokens->next();

    if (token.type == TokenType::Boolean) {
        constant.value.setBooleanValue(token.booleanValue);
        constant.type = context->getRuntime()->getBooleanType();

        return true;
    }

    if (token.type == TokenType::Integer) {
        constant.value.setIntegerValue(token.integerValue);
        constant.type = context->getRuntime()->getIntegerType();

        return true;
    }

    if (token.type == TokenType::Float) {
        constant.value.setFloatValue(token.floatValue);
        constant.type = context->getRuntime()->getFloatType();

        return true;
    }

    if (token.type == TokenType::String) {
        constant.value.setStringValue(token.textValue);
        constant.type = context->getRuntime()->getStringType();

        return true;
    }

    // Constant reference?
    if (token.type == TokenType::Identifier) {
        auto constantId = context->getRuntime()->getConstantId(token.textValue);

        if (constantId == NOMAD_INVALID_ID) {
            context->reportError("Unknown constant '" + token.textValue + "'", tokens);

            return false;
        }

        // `getConstantValue` borrows the runtime's value, so take an owned copy: `ConstantValue`
        // frees whatever it holds.
        RuntimeValue borrowedValue;
        context->getRuntime()->getConstantValue(constantId, borrowedValue);
        constant.type = context->getRuntime()->getConstantType(constantId);
        constant.type->copyValue(borrowedValue, constant.value);

        return true;
    }

    context->reportError("Unexpected token in constant expression: '" + token.textValue + "'", tokens);

    return false;
}

bool parseConstantBitwiseAnd(CompilerContext* context, Tokenizer* tokens, ConstantValue& constant);

bool parseConstantParentheses(CompilerContext* context, Tokenizer* tokens, ConstantValue& constant) {
    if (tokens->is("(")) {
        tokens->next();

        return parseConstantBitwiseAnd(context, tokens, constant) && tokens->expect(")");
    }

    return parseConstantPrimary(context, tokens, constant);
}

bool parseConstantUnaryOperator(CompilerContext* context, Tokenizer* tokens, ConstantValue& constant) {
    if (tokens->is("-") || tokens->is("+") || tokens->is("!")) {
        auto& token = tokens->next();

        UnaryOperator op;

        if (token.textValue == "-") {
            op = UnaryOperator::Minus;
        } else if (token.textValue == "+") {
            op = UnaryOperator::Plus;
        } else {
            op = UnaryOperator::Bang;
        }

        if (!parseConstantParentheses(context, tokens, constant)) {
            return false;
        }

        auto foldResult = context->getCompiler()->foldUnary(
            op,
            constant.type,
            constant.value,
            constant.value
        );

        if (!foldResult) {
            context->reportError("Failed to fold unary operator '" + token.textValue + "'", tokens);

            return false;
        }

        return true;
    }

    return parseConstantParentheses(context, tokens, constant);
}

bool foldConstantBinary(
    CompilerContext* context,
    Tokenizer* tokens,
    BinaryOperator op,
    const NomadString& operatorText,
    ConstantValue& constant,
    const ConstantValue& rhs
) {
    const auto foldResult = context->getCompiler()->foldBinary(
        op,
        constant.type,
        constant.value,
        rhs.type,
        rhs.value,
        constant.value
    );

    if (!foldResult) {
        context->reportError("Failed to fold binary operator '" + operatorText + "'", tokens);

        return false;
    }

    return true;
}

bool parseConstantProduct(CompilerContext* context, Tokenizer* tokens, ConstantValue& constant) {
    if (!parseConstantUnaryOperator(context, tokens, constant)) {
        return false;
    }

    while (tokens->is("*") || tokens->is("/") || tokens->is("%")) {
        auto& token = tokens->next();

        BinaryOperator op;

        if (token.textValue == "*") {
            op = BinaryOperator::Star;
        } else if (token.textValue == "/") {
            op = BinaryOperator::Slash;
        } else {
            op = BinaryOperator::Percent;
        }

        ConstantValue rhs;

        if (!parseConstantUnaryOperator(context, tokens, rhs) ||
            !foldConstantBinary(context, tokens, op, token.textValue, constant, rhs)) {
            return false;
        }
    }

    return true;
}

bool parseConstantTerm(CompilerContext* context, Tokenizer* tokens, ConstantValue& constant) {
    if (!parseConstantProduct(context, tokens, constant)) {
        return false;
    }

    while (tokens->is("+") || tokens->is("-")) {
        auto& token = tokens->next();

        const auto op = token.textValue == "+" ? BinaryOperator::Plus : BinaryOperator::Minus;

        ConstantValue rhs;

        if (!parseConstantProduct(context, tokens, rhs) ||
            !foldConstantBinary(context, tokens, op, token.textValue, constant, rhs)) {
            return false;
        }
    }

    return true;
}

bool parseConstantBitwiseOr(CompilerContext* context, Tokenizer* tokens, ConstantValue& constant) {
    if (!parseConstantTerm(context, tokens, constant)) {
        return false;
    }

    while (tokens->is("|")) {
        auto& token = tokens->next();

        ConstantValue rhs;

        if (!parseConstantTerm(context, tokens, rhs) ||
            !foldConstantBinary(context, tokens, BinaryOperator::Pipe, token.textValue, constant, rhs)) {
            return false;
        }
    }

    return true;
}

bool parseConstantBitwiseXor(CompilerContext* context, Tokenizer* tokens, ConstantValue& constant) {
    if (!parseConstantBitwiseOr(context, tokens, constant)) {
        return false;
    }

    while (tokens->is("^")) {
        auto& token = tokens->next();

        ConstantValue rhs;

        if (!parseConstantBitwiseOr(context, tokens, rhs) ||
            !foldConstantBinary(context, tokens, BinaryOperator::Caret, token.textValue, constant, rhs)) {
            return false;
        }
    }

    return true;
}

bool parseConstantBitwiseAnd(CompilerContext* context, Tokenizer* tokens, ConstantValue& constant) {
    if (!parseConstantBitwiseXor(context, tokens, constant)) {
        return false;
    }

    while (tokens->is("&")) {
        auto& token = tokens->next();

        ConstantValue rhs;

        if (!parseConstantBitwiseXor(context, tokens, rhs) ||
            !foldConstantBinary(context, tokens, BinaryOperator::And, token.textValue, constant, rhs)) {
            return false;
        }
    }

    return true;
}

bool parseConstantExpression(CompilerContext* context, Tokenizer* tokens, ConstantValue& constant) {
    if (!parseConstantBitwiseAnd(context, tokens, constant)) {
        return false;
    }

    if (!tokens->endOfLine()) {
        context->reportError("Unexpected token in constant expression: '" + tokens->currentToken().textValue + "'", tokens);

        return false;
    }

    return true;
}

// Parses `<name> =` at the start of a `const` statement.
bool parseConstantName(CompilerContext* context, Tokenizer* tokens, NomadString& constantName) {
    if (tokens->endOfLine()) {
        context->reportError("Identifier expected", tokens);

        return false;
    }

    auto& constantToken = tokens->next();

    if (constantToken.type != TokenType::Identifier) {
        context->reportError("Identifier expected. Got '" + constantToken.textValue + "' instead", tokens);

        return false;
    }

    constantName = constantToken.textValue;

    return tokens->expect(TokenType::Operator) && tokens->expect("=");
}

bool preParseConstStatement(CompilerContext* context, Function* /*function*/, Tokenizer* tokens) {
    NomadString constantName;

    if (!parseConstantName(context, tokens, constantName)) {
        return false;
    }

    if (context->getRuntime()->getConstantId(constantName) != NOMAD_INVALID_ID) {
        context->reportError("Constant '" + constantName + "' is already defined", tokens);

        return false;
    }

    ConstantValue constant;

    if (!parseConstantExpression(context, tokens, constant)) {
        return false;
    }

    context->getRuntime()->registerConstant(constantName, constant.value, constant.type);

    return true;
}

std::unique_ptr<Statement> parseConstStatement(CompilerContext* context, Function* function, Tokenizer* tokens) {
    NomadString constantName;

    if (!parseConstantName(context, tokens, constantName)) {
        return nullptr;
    }

    // Parse initializer as a regular expression so AST contains the expression node
    auto initializer = parser::parseExpression(context, function, tokens);

    if (initializer == nullptr) {
        return nullptr;
    }

    return std::make_unique<ConstDeclaration>(
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        constantName,
        std::move(initializer)
    );
}

std::unique_ptr<Statement> parseEventStatement(CompilerContext* context, Function* /*function*/, Tokenizer* tokens) {
    // Event declarations were registered during pre-parse; create AST node for tooling/analysis.
    if (!tokens->is(TokenType::Identifier)) {
        context->reportError("Event name expected", tokens);

        return nullptr;
    }

    const auto eventName = tokens->next().textValue;

    // Parse parameters (name:type ...)
    auto eventDecl = std::make_unique<EventDeclaration>(tokens->getLineIndex(), tokens->getColumnIndex(), eventName);

    while (!tokens->endOfLine()) {
        NomadString paramName, typeName;

        if (!tokens->nextIdentifier(paramName) || !tokens->expect(":") || !tokens->nextIdentifier(typeName)) {
            return nullptr;
        }

        const auto type = context->getRuntime()->getTypeByName(typeName);

        if (type == nullptr) {
            context->reportError("Unknown type '" + typeName + "' for parameter '" + paramName + "'", tokens);

            return nullptr;
        }

        if (type->isInternal()) {
            context->reportError("Internal type '" + typeName + "' cannot be used for parameter '" + paramName + "'", tokens);

            return nullptr;
        }

        eventDecl->addParam(std::make_unique<ParameterDeclaration>(tokens->getLineIndex(), tokens->getColumnIndex(), paramName, type));
    }

    return eventDecl;
}

std::unique_ptr<Statement> parseOnStatement(CompilerContext* context, Function* function, Tokenizer* tokens) {
    // Syntax: on <eventName> fun <params> <body> end  OR on <eventName> <functionName>
    if (!tokens->is(TokenType::Identifier)) {
        context->reportError("Event name expected", tokens);

        return nullptr;
    }

    const auto eventName = tokens->next().textValue;

    // Expect 'fun' keyword or identifier for function name
    if (tokens->is("fun")) {
        tokens->next();

        // Inline handler: parse parameters and body as a generated function
        const auto handlerFunctionName = generateFunFunctionName(context, function, tokens->getLineIndex());
        auto handlerFunctionId = context->getCompiler()->registerFunctionSource(handlerFunctionName, function->getPath(), function->getSource());
        auto handlerFunction = context->getRuntime()->getFunction(handlerFunctionId);

        // Parse parameters (names only for now)
        const auto parametersParsed = preParseParamsStatement(context, handlerFunction, tokens);

        auto handlerBody = parseFunBody(context, handlerFunction, tokens);

        if (!parametersParsed || handlerBody == nullptr) {
            return nullptr;
        }

        context->getCompiler()->setFunctionNode(handlerFunctionId, std::move(handlerBody));

        // Create FunctionDecl for handler
        auto handlerDecl = std::make_unique<FunctionDeclaration>(tokens->getLineIndex(), tokens->getColumnIndex(), handlerFunctionName);

        return std::make_unique<EventHandlerDeclaration>(tokens->getLineIndex(), tokens->getColumnIndex(), eventName, std::move(handlerDecl));
    }

    // Named handler reference
    NomadString handlerName;

    if (!tokens->nextIdentifier(handlerName)) {
        return nullptr;
    }

    const auto handlerFunctionId = context->getRuntime()->getFunctionId(handlerName);

    if (handlerFunctionId == NOMAD_INVALID_ID) {
        context->reportError("Unknown handler function '" + handlerName + "'", tokens);

        return nullptr;
    }

    // Create a lightweight handler decl that references the existing function name
    auto handlerDecl = std::make_unique<FunctionDeclaration>(tokens->getLineIndex(), tokens->getColumnIndex(), handlerName);

    return std::make_unique<EventHandlerDeclaration>(tokens->getLineIndex(), tokens->getColumnIndex(), eventName, std::move(handlerDecl));
}

class IfStatementNode : public Statement {
private:
    struct Branch {
        std::unique_ptr<Expression> expression;
        StatementList statements;
    };

public:
    IfStatementNode(NomadIndex line, NomadIndex column) :
        Statement(line, column)
    {
    }

    void setExpression(std::unique_ptr<Expression> expression) {
        m_expression = std::move(expression);
    }

    StatementList* getBody() { return &m_body; }

    static void resolveCondition(CompilerContext* context, Function* function, Expression* condition) {
        if (!condition->resolve(context, function)) {
            return;
        }

        if (condition->getType() != context->getRuntime()->getBooleanType()) {
            context->reportError(
                "If statement condition must be a boolean expression",
                function != nullptr ? function->getPath() : NomadString{},
                condition->getLine(),
                condition->getColumn()
            );
        }
    }

    void addBranch(std::unique_ptr<Expression> expression, StatementList statements) {
        auto branch = Branch {
            .expression = std::move(expression),
            .statements = std::move(statements)
        };

        m_branches.push_back(std::move(branch));
    }

    void setElseStatements(StatementList statements) {
        m_elseStatements = std::move(statements);
    }

    // Without an `else`, execution can skip every branch.
    [[nodiscard]] NomadBoolean alwaysReturns() const override {
        if (m_elseStatements.isEmpty() || !m_elseStatements.alwaysReturns() || !m_body.alwaysReturns()) {
            return false;
        }

        return std::ranges::all_of(m_branches, [](const Branch& branch) {
            return branch.statements.alwaysReturns();
        });
    }

    void checkReachability(CompilerContext* context, const Function* function) const override {
        m_body.checkReachability(context, function);

        for (const auto& branch : m_branches) {
            branch.statements.checkReachability(context, function);
        }

        m_elseStatements.checkReachability(context, function);
    }

protected:
    void onResolve(CompilerContext* context, Function* function) override {
        resolveCondition(context, function, m_expression.get());
        (void)m_body.resolve(context, function);

        for (const auto& branch : m_branches) {
            resolveCondition(context, function, branch.expression.get());
            (void)branch.statements.resolve(context, function);
        }

        (void)m_elseStatements.resolve(context, function);
    }

    void onCompile(Compiler* compiler, Function* function) override {
        std::vector<size_t> jumpEndIndices;

        m_expression->compile(compiler, function, ExpressionTarget::Result);

        compiler->addOpCode(OpCodes::op_jump_if_false);
        const auto jumpInstructionIndex = compiler->addIndex(NOMAD_INVALID_INDEX); // Placeholder for jump address.

        m_body.compile(compiler, function);

        if (!m_branches.empty() || !m_elseStatements.isEmpty()) {
            // Jump to end...
            compiler->addOpCode(OpCodes::op_jump);

            const auto ifJumpIndex = compiler->addIndex(NOMAD_INVALID_INDEX); // Placeholder for jump

            jumpEndIndices.push_back(ifJumpIndex);
        }

        compiler->setIndex(jumpInstructionIndex, compiler->getOpCodeSize());

        for (auto branch = m_branches.begin(); branch != m_branches.end(); ++branch) {
            branch->expression->compile(compiler, function, ExpressionTarget::Result);

            compiler->addOpCode(OpCodes::op_jump_if_false);
            const auto elseIfJumpInstructionIndex = compiler->addIndex(NOMAD_INVALID_INDEX); // Placeholder for jump address.

            branch->statements.compile(compiler, function);

            // Jump to end...
            if (branch + 1 != m_branches.end() || !m_elseStatements.isEmpty()) {
                compiler->addOpCode(OpCodes::op_jump);
                auto elseIfJumpIndex = compiler->addIndex(NOMAD_INVALID_INDEX); // Placeholder for jump

                jumpEndIndices.push_back(elseIfJumpIndex);
            }

            compiler->setIndex(elseIfJumpInstructionIndex, compiler->getOpCodeSize());
        }

        if (m_elseStatements.getStatementCount() > 0) {
            m_elseStatements.compile(compiler, function);
        }

        // Resolve jump addresses to end of if statement
        for (const auto index: jumpEndIndices) {
            compiler->setIndex(index, compiler->getOpCodeSize());
        }
    }

private:
    std::unique_ptr<Expression> m_expression;
    StatementList m_body;
    std::vector<Branch> m_branches;
    StatementList m_elseStatements;
};

std::unique_ptr<Statement> parseIfStatement(CompilerContext* context, Function* function, Tokenizer* tokens) {
    auto ifStatement = std::make_unique<IfStatementNode>(tokens->getLineIndex(), tokens->getColumnIndex());

    // Keep parsing after a condition error so the blocks and `end` are consumed by this statement.
    auto valid = true;

    auto ifExpression = parser::parseExpression(context, function, tokens);

    if (ifExpression == nullptr || !parser::expectEndOfLine(context, function, tokens)) {
        valid = false;
    }

    ifStatement->setExpression(std::move(ifExpression));

    tokens->nextLine();

    valid = parser::parseBlock(context, function, tokens, {"else", "end"}, ifStatement->getBody()) && valid;

    while (tokens->is("else")) {
        // Make sure there's nothing else after `else`
        tokens->next();

        if (tokens->is("if")) {
            // Skip over 'elseIf'
            tokens->next();

            auto branchExpression = parser::parseExpression(context, function, tokens);
            auto branchStatements = StatementList();

            if (branchExpression == nullptr || !parser::expectEndOfLine(context, function, tokens)) {
                valid = false;
            }

            tokens->nextLine();

            valid = parser::parseBlock(context, function, tokens, {"else", "end"}, &branchStatements) && valid;

            ifStatement->addBranch(std::move(branchExpression), std::move(branchStatements));
        } else {
            if (!parser::expectEndOfLine(context, function, tokens)) {
                valid = false;
            }

            // Skip over 'else'
            tokens->nextLine();

            StatementList elseStatements;

            valid = parser::parseBlock(context, function, tokens, {"end"}, &elseStatements) && valid;

            ifStatement->setElseStatements(std::move(elseStatements));

            // Break out of 'else' while loop
            break;
        }
    }

    if (!tokens->is("end")) {
        // parseBlock already reported an unexpected end of file.
        if (!tokens->isEndOfFile()) {
            context->reportError("Expected 'end' to close 'if' statement.", tokens);
        }

        return nullptr;
    }

    tokens->next(); // Consume 'end'

    if (!valid) {
        return nullptr;
    }

    return ifStatement;
}

bool preParseParamsStatement(CompilerContext* context, Function* function, Tokenizer* tokens) {
    if (function->getParameterCount() > 0) {
        context->reportError("Function '" + function->getName() + "' already has parameters", tokens);

        return false;
    }

    while (!tokens->endOfLine()) {
        NomadString parameterName, parameterTypeName;

        if (!tokens->nextIdentifier(parameterName) ||
            !tokens->expect(":") ||
            !tokens->nextIdentifier(parameterTypeName)) {
            return false;
        }

        if (function->getParameterId(parameterName) != NOMAD_INVALID_ID) {
            context->reportError("Parameter '" + parameterName + "' is already defined", tokens);

            return false;
        }

        auto parameterType = context->getRuntime()->getTypeByName(parameterTypeName);

        if (parameterType == nullptr) {
            context->reportError("Unknown type '" + parameterTypeName + "' for parameter", tokens);

            return false;
        }

        if (parameterType->isInternal()) {
            context->reportError("Internal type '" + parameterTypeName + "' cannot be used for parameter '" + parameterName + "'", tokens);

            return false;
        }

        function->addParameter(parameterName, parameterType);
    }

    return true;
}

} // nomad
