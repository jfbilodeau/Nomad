// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/String.hpp>

#include <nomad/compiler/Parser.hpp>

#include <nomad/compiler/Argument.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/compiler/StatementParsers.hpp>
#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/compiler/statements/ReturnStatement.hpp>

namespace nomad {

namespace parser {

using ParseExpressionFunction = std::unique_ptr<Expression> (*)(CompilerContext*, Function*, Tokenizer*, const Expression*);

std::unique_ptr<Expression> parseBinaryOperatorExpression(
    CompilerContext* context,
    Function* function,
    Tokenizer* tokens,
    const Expression* parent,
    std::vector<BinaryOperator> operators,
    const ParseExpressionFunction nextParseFn
) {
    auto expression = nextParseFn(context, function, tokens, parent);

    if (expression == nullptr) {
        return nullptr;
    }

    while (true) {
        if (!tokens->is(TokenType::Operator)) {
            return expression;
        }

        auto& operatorToken = tokens->currentToken();

        BinaryOperator op = getBinaryOperator(operatorToken.textValue);

        auto opIterator = std::ranges::find(operators, op);

        if (opIterator != operators.end()) {
            tokens->next();

            auto right = nextParseFn(context, function, tokens, parent);

            if (right == nullptr) {
                return nullptr;
            }

            expression = std::make_unique<BinaryExpression>(
                parent,
                tokens->getLineIndex(),
                tokens->getColumnIndex(),
                op,
                std::move(expression),
                std::move(right)
            );
        } else {
            return expression;
        }
    }
}

std::unique_ptr<Statement> parseLine(CompilerContext* context, Function* function, Tokenizer* tokenizer) {
    if (!tokenizer->isReportingTo(context)) {
        context->getCompiler()->reportInternalError("Parser and tokenizer must share the same compiler context");
    }

    if (tokenizer->endOfLine()) {
        // Empty line
        return nullptr;
    }

    if (tokenizer->getTokenCount() >= 3 && tokenizer->getTokenAt(1) == "=") {
        return parseAssignmentStatement(context, function, tokenizer);
    }

    return parseStatement(context, function, tokenizer);
}

bool parseBlock(
    CompilerContext* context,
    Function* function,
    Tokenizer* tokens,
    const std::vector<NomadString>& endTokens,
    StatementList* statements
) {
    const auto errorCount = context->getErrorCount();

    while (true) {
        if (tokens->isEndOfFile()) {
            context->reportError("Unexpected end of file", tokens);

            return false;
        }

        const auto& token = tokens->currentToken();

        if (std::ranges::find(endTokens, token.textValue) != endTokens.end()) {
            return context->getErrorCount() == errorCount;
        }

        auto statement = parseLine(context, function, tokens);

        if (statement != nullptr) {
            statements->addStatement(std::move(statement));
        }

        tokens->nextLine();
    }
}

std::unique_ptr<Statement> parseStatement(
    CompilerContext* context,
    Function* function,
    Tokenizer* tokens
) {
    auto& statementToken = tokens->next();
    const auto statementName = statementToken.textValue;

    ParseStatementFn statementFn;

    if (context->getCompiler()->getParseStatementFn(statementName, statementFn)) {
        if (statementFn == nullptr) {
            return nullptr;
        }

        auto currentLine = tokens->getLineIndex();

        auto statement = statementFn(context, function, tokens);

        if (statement == nullptr) {
            return nullptr;
        }

        if (tokens->getLineIndex() == currentLine && !expectEndOfLine(context, function, tokens)) {
            return nullptr;
        }

        return statement;
    }

    NativeFunctionDefinition nativeFunction;
    if (context->getRuntime()->getNativeFunctionDefinition(statementName, nativeFunction)) {
        auto statement = parseNativeFunctionStatement(context, function, tokens, nativeFunction);

        if (statement == nullptr || !expectEndOfLine(context, function, tokens)) {
            return nullptr;
        }

        return statement;
    }

    auto functionId = context->getRuntime()->getFunctionId(statementName);

    if (functionId != NOMAD_INVALID_ID) {
        auto statement = parseFunctionCall(context, function, tokens, statementName);

        if (statement == nullptr || !expectEndOfLine(context, function, tokens)) {
            return nullptr;
        }

        return statement;
    }

    context->reportError("Unknown statement, native function or function name: '" + statementName + "'", tokens);

    return nullptr;
}

std::unique_ptr<Statement> parseNativeFunctionStatement(CompilerContext* context, Function* function, Tokenizer* tokens, const NativeFunctionDefinition& nativeFunction) {
    auto line = tokens->getLineIndex();
    auto column = tokens->getColumnIndex();

    auto nativeFunctionStatement = std::make_unique<NativeFunctionStatementNode>(
        line,
        column,
        nativeFunction
    );

    if (!parseNativeFunctionArguments(context, function, tokens, nativeFunction, nativeFunctionStatement->getArguments())) {
        return nullptr;
    }

    return nativeFunctionStatement;
}

NomadId parseCallbackParameter(
    CompilerContext* context,
    Function* function,
    Tokenizer* tokens,
    const FunctionType* callbackType)
{
    const auto isFun = tokens->is("fun");

    if (isFun || tokens->is("then")) {
        if (context->getMode() == CompilerMode::Interpreter) {
            context->reportError(
                NomadString("Inline `") + (isFun ? "fun" : "then") + "` callbacks are not supported by the interpreter"
            , tokens);

            return NOMAD_INVALID_ID;
        }

        tokens->next();

        const auto functionName = generateFunFunctionName(context, function, tokens->getLineIndex());
        auto& functionPath = function->getPath();
        auto& functionSource = function->getSource();

        const auto functionId = context->getCompiler()->registerFunctionSource(functionName, functionPath, functionSource);

        const auto callbackFunction = context->getRuntime()->getFunction(functionId);

        // Wire the nested function's parent reference.
        callbackFunction->setParentId(function->getId());

        auto parametersParsed = parseCallbackFunParameters(context, callbackFunction, tokens, callbackType);

        if (parametersParsed) {
            parametersParsed = tokens->expectEndOfLine();
        } else {
            // Still parse the body so its lines are not reported as top-level statements.
            tokens->nextLine();
        }

        auto callbackBody = isFun
            ? parseFunBody(context, callbackFunction, tokens)
            : parseThenBody(context, callbackFunction, tokens);

        if (!parametersParsed || callbackBody == nullptr) {
            return NOMAD_INVALID_ID;
        }

        context->getCompiler()->setFunctionNode(functionId, std::move(callbackBody));

        return functionId;
    }

    NomadString funName;

    if (!tokens->nextIdentifier(funName)) {
        return NOMAD_INVALID_ID;
    }

    const auto funId = context->getRuntime()->getFunctionId(funName);

    if (funId == NOMAD_INVALID_ID) {
        context->reportError("Unknown callback function name '" + funName + "'", tokens);

        return NOMAD_INVALID_ID;
    }

    if (context->getRuntime()->getFunction(funId)->getParameterCount() != callbackType->getParameterCount()) {
        context->reportError("Callback function '" + funName + "' has incorrect number of parameters", tokens);

        return NOMAD_INVALID_ID;
    }

    for (auto i = NomadIndex{0}; i < callbackType->getParameterCount(); ++i) {
        const auto parameterId = toNomadId(i);
        const auto expectedType = callbackType->getParameterType(parameterId);
        const auto actualType = context->getRuntime()->getFunction(funId)->getParameterType(parameterId);
        if (expectedType != actualType) {
            context->reportError("Callback function '" + funName + "' parameter type mismatch at index " + toString(i), tokens);

            return NOMAD_INVALID_ID;
        }
    }

    return funId;
}

std::unique_ptr<Statement> parseFunctionCall(
    CompilerContext* context,
    Function* function,
    Tokenizer* tokens,
    const NomadString& functionName
) {
    auto functionCallStatement = std::make_unique<FunctionCallStatementNode>(
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        functionName
    );

    const auto functionId = context->getRuntime()->getFunctionId(functionName);

    if (functionId == NOMAD_INVALID_ID) {
        context->reportError("Unknown function name: '" + functionName + "'", tokens);

        return nullptr;
    }

    const auto targetFunction = context->getRuntime()->getFunction(functionId);

    if (!parseFunctionArguments(context, function, tokens, targetFunction, functionCallStatement->getArguments())) {
        return nullptr;
    }

    return functionCallStatement;
}

std::unique_ptr<Statement> parseReturnStatement(CompilerContext* context, Function* function, Tokenizer* tokens) {
    auto expression = parseExpression(context, function, tokens);

    if (expression == nullptr) {
        return nullptr;
    }

    return std::make_unique<ReturnStatementNode>(tokens->getLineIndex(), tokens->getColumnIndex(), std::move(expression));
}

std::unique_ptr<Expression> parseExpression(CompilerContext* context, Function* function, Tokenizer* tokens) {
    return parseLogicalAndExpression(context, function, tokens, nullptr);
}

std::unique_ptr<Expression> parseLogicalAndExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent) {
    return parseBinaryOperatorExpression(
        context,
        function,
        tokens,
        parent,
        {BinaryOperator::AndAnd},
        &parseLogicalOrExpression
    );
}


std::unique_ptr<Expression> parseLogicalOrExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent) {
    return parseBinaryOperatorExpression(
        context,
        function,
        tokens,
        parent,
        {BinaryOperator::PipePipe},
        &parseRelationalExpression
    );
}

std::unique_ptr<Expression> parseRelationalExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent) {
    return parseBinaryOperatorExpression(
        context,
        function,
        tokens,
        parent,
        {
            BinaryOperator::EqualEqual,
            BinaryOperator::BangEqual,
            BinaryOperator::LessThan,
            BinaryOperator::LessThanEqual,
            BinaryOperator::GreaterThan,
            BinaryOperator::GreaterThanEqual,
        },
        &parseBitwiseAndExpression
    );
}

std::unique_ptr<Expression> parseBitwiseAndExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent) {
    return parseBinaryOperatorExpression(
        context,
        function,
        tokens,
        parent,
        {
            BinaryOperator::And,
        },
        &parseBitwiseXorExpression
    );
}

std::unique_ptr<Expression> parseBitwiseXorExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent) {
    return parseBinaryOperatorExpression(
        context,
        function,
        tokens,
        parent,
        {
            BinaryOperator::Caret,
        },
        &parseBitwiseOrExpression
    );
}

std::unique_ptr<Expression> parseBitwiseOrExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent) {
    return parseBinaryOperatorExpression(
        context,
        function,
        tokens,
        parent,
        {
            BinaryOperator::Pipe,
        },
        &parseTermExpression
    );
}

std::unique_ptr<Expression> parseTermExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent) {
    return parseBinaryOperatorExpression(
        context,
        function,
        tokens,
        parent,
        {
            BinaryOperator::Plus,
            BinaryOperator::Minus,
        },
        &parseProductExpression
    );
}

std::unique_ptr<Expression> parseProductExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent) {
    return parseBinaryOperatorExpression(
        context,
        function,
        tokens,
        parent,
        {
            BinaryOperator::Star,
            BinaryOperator::Slash,
            BinaryOperator::Percent,
        },
        &parseParenthesesExpression
    );
}

std::unique_ptr<Expression> parseParenthesesExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent) {
    if (tokens->is("(")) {
        tokens->next();

        auto expression = parseExpression(context, function, tokens);

        if (expression == nullptr || !tokens->expect(")")) {
            return nullptr;
        }

        return expression;
    }

    return parseUnaryOperatorExpression(context, function, tokens, parent);
}

std::unique_ptr<Expression> parseUnaryOperatorExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent) {
    if (tokens->endOfLine()) {
        context->reportError("Unexpected end of line", tokens);

        return nullptr;
    }

    auto token = tokens->currentToken();

    auto op = getUnaryOperator(token.textValue);

    if (op == UnaryOperator::Unknown) {
        return parsePrimaryExpression(context, function, tokens, parent);
    }

    tokens->next();

    std::unique_ptr<Expression> expression;

    if (tokens->is("(")) {
        tokens->next();

        expression = parseExpression(context, function, tokens);

        if (expression == nullptr || !tokens->expect(")")) {
            return nullptr;
        }
    } else {
        expression = parsePrimaryExpression(context, function, tokens, parent);

        if (expression == nullptr) {
            return nullptr;
        }
    }

    return std::make_unique<UnaryExpression>(
        parent,
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        op,
        std::move(expression)
    );
}

bool expectEndOfLine(CompilerContext* context, Function* /*function*/, Tokenizer* tokens) {
    if (!tokens->endOfLine()) {
        context->reportError("Expected end of line", tokens);

        return false;
    }

    return true;
}

IdentifierType getIdentifierType(const Compiler* compiler, const NomadString& name, const Function* function) {
    if (compiler->getRuntime()->getKeywordId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::Keyword;
    }

    if (compiler->isStatement(name)) {
        return IdentifierType::Statement;
    }

    NativeFunctionDefinition nativeFunctionDefinition;

    if (compiler->getRuntime()->getNativeFunctionDefinition(name, nativeFunctionDefinition)) {
        return IdentifierType::NativeFunction;
    }

    if (compiler->getRuntime()->getConstantId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::Constant;
    }

    if (compiler->getRuntime()->getFunctionId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::Function;
    }

    if (compiler->getRuntime()->getDynamicVariableId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::DynamicVariable;
    }

    if (compiler->getRuntime()->getVariableContextIdByPrefix(name) != NOMAD_INVALID_ID) {
        return IdentifierType::ContextVariable;
    }

    if (function && function->getVariableId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::FunctionVariable;
    }

    return IdentifierType::Unknown;
}

std::unique_ptr<Statement> parseAssignmentStatement(CompilerContext* context, Function* function, Tokenizer* tokens) {
    auto& variableName = tokens->currentToken().textValue;

    IdentifierDefinition identifierDefinition;
    context->getCompiler()->getIdentifierDefinition(variableName, function, identifierDefinition);

    if (identifierDefinition.identifierType == IdentifierType::Unknown && function != nullptr) {
        // VirtualMachine input has no function: console variables are defined when the assignment is evaluated.
        // Context variable?
        const auto contextId = context->getRuntime()->getVariableContextIdByPrefix(variableName);

        if (contextId != NOMAD_INVALID_ID) {
            // Register new context variable
            const auto variableId = context->getRuntime()->registerOrUpdateContextVariable(variableName, nullptr);

            if (variableId == NOMAD_INVALID_ID) {
                context->getCompiler()->reportInternalError("Failed to register context variable: " + variableName);
            }
        } else {
            // Regular function variable.
            function->registerVariable(variableName, nullptr);
        }
    }

    tokens->next(); // Consume variable name

    if (!tokens->expect("=")) {
        return nullptr;
    }

    auto expression = parseExpression(context, function, tokens);

    if (expression == nullptr || !expectEndOfLine(context, function, tokens)) {
        return nullptr;
    }

    return std::make_unique<AssignmentStatement>(
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        variableName,
        std::move(expression)
    );
}

std::unique_ptr<Expression> parsePrimaryExpression(CompilerContext* context, Function* function, Tokenizer* tokens, const Expression* parent) {
    if (tokens->endOfLine()) {
        context->reportError("Unexpected end of line", tokens);

        return nullptr;
    }

    auto& token = tokens->next();

    if (token.type == TokenType::FormatString) {
        return std::make_unique<FormatStringLiteral>(
            parent,
            tokens->getLineIndex(),
            tokens->getColumnIndex(),
            token.textValue
        );
    }

    if (token.type == TokenType::String) {
        return std::make_unique<StringLiteral>(
            parent,
            tokens->getLineIndex(),
            tokens->getColumnIndex(),
            token.textValue
        );
    }

    if (token.type == TokenType::Integer) {
        return std::make_unique<IntegerLiteral>(
            parent,
            tokens->getLineIndex(),
            tokens->getColumnIndex(),
            token.integerValue
        );
    }

    if (token.type == TokenType::Float) {
        return std::make_unique<FloatLiteral>(
            parent,
            tokens->getLineIndex(),
            tokens->getColumnIndex(),
            token.floatValue
        );
    }

    // auto& identifier = token.textValue;

    if (token.type == TokenType::Identifier) {
        // Is it a nativeFunction?
        NativeFunctionDefinition nativeFunction;
        if (context->getRuntime()->getNativeFunctionDefinition(token.textValue, nativeFunction)) {
            if (nativeFunction.returnType->isVoid()) {
                context->reportError(
                    "Cannot use native function '" + token.textValue + "' in an expression because it does not return a value"
                , tokens);

                return nullptr;
            }

            return parseNativeFunctionCallExpression(context, function, tokens, parent, nativeFunction);
        }

        // Is it a function?
        const auto functionId = context->getRuntime()->getFunctionId(token.textValue);

        if (functionId != NOMAD_INVALID_ID) {
            return parseFunctionCallExpression(context, function, tokens, parent, token.textValue);
        }

        // Constant?
        const auto constantId = context->getRuntime()->getConstantId(token.textValue);

        if (constantId != NOMAD_INVALID_ID) {
            return std::make_unique<ConstantValueExpression>(
                parent,
                tokens->getLineIndex(),
                tokens->getColumnIndex(),
                token.textValue
            );
        }

        if (context->getMode() == CompilerMode::Interpreter &&
            context->getCompiler()->getIdentifierType(token.textValue, function) == IdentifierType::Unknown &&
            !tokens->endOfLine()) {
            const auto nextType = tokens->currentToken().type;
            if (nextType == TokenType::Identifier ||
                nextType == TokenType::Boolean ||
                nextType == TokenType::Integer ||
                nextType == TokenType::Float ||
                nextType == TokenType::String ||
                nextType == TokenType::FormatString ||
                tokens->is("(")) {
                context->reportError("Unknown function '" + token.textValue + "'", tokens);
                return nullptr;
            }
        }

        // Identifier
        return std::make_unique<IdentifierExpression>(
            parent,
            tokens->getLineIndex(),
            tokens->getColumnIndex(),
            token.textValue
        );
    }

    context->reportError("Unexpected token '" + token.textValue + "'", tokens);

    return nullptr;
}

std::unique_ptr<Expression> parseFunctionCallExpression(
    CompilerContext* context,
    Function* function,
    Tokenizer* tokens,
    const Expression* parent,
    const NomadString& functionName
) {
    auto functionCallExpression = std::make_unique<FunctionCallExpression>(
        parent,
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        functionName
    );

    const auto functionId = context->getRuntime()->getFunctionId(functionName);
    const auto targetFunction = context->getRuntime()->getFunction(functionId);

    if (!parseFunctionArguments(context, function, tokens, targetFunction, functionCallExpression->getArguments())) {
        return nullptr;
    }

    return functionCallExpression;
}

bool parseFunctionArguments(
    CompilerContext* context,
    Function* function,
    Tokenizer* tokens,
    const Function* targetFunction,
    ArgumentList* arguments
) {
    const auto parameterCount = targetFunction->getParameterCount();

    for (auto i = NomadIndex{0}; i < parameterCount; ++i) {
        const auto parameterId = toNomadId(i);
        const auto& parameterName = targetFunction->getParameterName(parameterId);
        const auto parameterType = targetFunction->getParameterType(parameterId);

        if (tokens->endOfLine()) {
            context->reportError("Expected argument '" + parameterName + "'", tokens);

            return false;
        }

        const auto argumentType = parameterType;

        const auto callbackType = argumentType->asCallback();

        if (callbackType) {
            if (tokens->is("then") && i + NomadIndex{1} < parameterCount) {
                context->reportError("`then` callback must be the last argument", tokens);

                return false;
            }

            const auto functionId = parseCallbackParameter(context, function, tokens, callbackType);

            if (functionId == NOMAD_INVALID_ID) {
                return false;
            }

            auto funArgument = createCallbackArgument(
                tokens->getLineIndex(),
                tokens->getColumnIndex(),
                argumentType,
                functionId
            );

            arguments->add(std::move(funArgument));
        } else {
            // Functions cannot assign to their parameters, so borrowable strings are lent rather than copied.
            auto argument = parseExpressionArgument(context, argumentType, function, tokens, ParameterAccess::ReadOnly);

            if (argument == nullptr) {
                return false;
            }

            arguments->add(std::move(argument));
        }
    }

    return true;
}

std::unique_ptr<Expression> parseNativeFunctionCallExpression(
    CompilerContext* context,
    Function* function,
    Tokenizer* tokens,
    const Expression* parent,
    const NativeFunctionDefinition& nativeFunction
) {
    auto nativeFunctionExpression = std::make_unique<CallNativeFunctionExpression>(
        parent,
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        nativeFunction.name
    );

    if (!parseNativeFunctionArguments(context, function, tokens, nativeFunction, nativeFunctionExpression->getArguments())) {
        return nullptr;
    }

    return nativeFunctionExpression;
}

// Returns false on error.
bool parseEventDispatch(CompilerContext* context, Function* function, Tokenizer* tokens, std::vector<std::unique_ptr<Argument>>& arguments) {
    NomadString eventName;

    if (!tokens->nextIdentifier(eventName)) {
        return false;
    }

    const auto eventId = context->getRuntime()->getEventId(eventName);

    if (eventId == NOMAD_INVALID_ID) {
        context->reportError("Unknown event name '" + eventName + "'", tokens);

        return false;
    }

    const auto event = context->getRuntime()->getEventDefinition(eventId);

    if (!event) {
        context->getCompiler()->reportInternalError("Unknown event declaration '" + eventName + "'");
    }

    auto eventArgument = createEventDispatchArgument(
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        context->getRuntime()->getEventDispatchType(),
        *event
    );

    arguments.push_back(std::move(eventArgument));

    for (auto& eventParameter: event->parameters) {
        const auto eventParameterType = context->getRuntime()->getType(eventParameter.typeId);

        if (!eventParameterType) {
            context->getCompiler()->reportInternalError("Invalid type id in event: " + toString(eventParameter.typeId) + " (" + eventParameter.typeName + ")");
        }

        auto expression = parseExpression(context, function, tokens);

        if (expression == nullptr) {
            return false;
        }

        auto argument = createExpressionArgument(
            tokens->getLineIndex(),
            tokens->getColumnIndex(),
            *eventParameterType,
            std::move(expression)
        );

        arguments.push_back(std::move(argument));
    }

    return true;
}

bool parseNativeFunctionArguments(
    CompilerContext* context,
    Function* function,
    Tokenizer* tokens,
    const NativeFunctionDefinition& nativeFunction,
    ArgumentList* arguments
) {
    const auto* runtime = context->getRuntime();

    for (NomadIndex i = 0; i < nativeFunction.parameters.size(); ++i) {
        const auto& parameter = nativeFunction.parameters[i];

        // Hidden source parameters are supplied by the compiler and consume no tokens.
        const auto isSourceParameter =
            parameter.type == runtime->getFileNameType() ||
            parameter.type == runtime->getFunctionNameType() ||
            parameter.type == runtime->getLineNumberType();

        if (!isSourceParameter && tokens->endOfLine()) {
            context->reportError("Expected arguments for `" + parameter.name + ":" + parameter.type->getTypeName() + "`", tokens);

            return false;
        }

        const auto argumentType = parameter.type;

        if (parameter.type == context->getRuntime()->getFileNameType()) {
            auto argument = createFileNameArgument(
                tokens->getLineIndex(),
                tokens->getColumnIndex(),
                parameter.type,
                function->getPath()
            );

            arguments->add(std::move(argument));
        } else if (parameter.type == context->getRuntime()->getFunctionNameType()) {
            auto argument = createFunctionNameArgument(
                tokens->getLineIndex(),
                tokens->getColumnIndex(),
                parameter.type,
                function->getName()
            );
            arguments->add(std::move(argument));
        } else if (parameter.type == context->getRuntime()->getLineNumberType()) {
            auto argument = createLineNumberArgument(
                tokens->getLineIndex(),
                tokens->getColumnIndex(),
                context->getRuntime()->getIntegerType()
            );
            arguments->add(std::move(argument));
        } else if (parameter.type == context->getRuntime()->getEventDispatchType()) {
            std::vector<std::unique_ptr<Argument>> eventDispatchArguments;

            if (!parseEventDispatch(context, function, tokens, eventDispatchArguments)) {
                return false;
            }

            arguments->addAll(std::move(eventDispatchArguments));
        } else if (parameter.type == context->getRuntime()->getEventCallbackType()) {
            NomadString eventName;

            if (!tokens->nextIdentifier(eventName)) {
                return false;
            }

            const auto eventId = context->getRuntime()->getEventId(eventName);

            if (eventId == NOMAD_INVALID_ID) {
                context->reportError("Unknown event name '" + eventName + "'", tokens);

                return false;
            }

            const auto event = context->getRuntime()->getEventDefinition(eventId);

            if (!event) {
                context->getCompiler()->reportInternalError("Unknown event declaration '" + eventName + "'");
            }

            auto& eventParameters = event->parameters;

            std::vector<const Type*> eventParameterTypes(eventParameters.size());

            std::ranges::transform(
                eventParameters,
                eventParameterTypes.begin(),
                [context](const auto& p) {
                    if (auto type = context->getRuntime()->getType(p.typeId)) {
                        return *type;
                    }

                    context->getCompiler()->reportInternalError("Invalid type id in event: " + toString(p.typeId) + " (" + p.typeName + ")");
                }
            );

            const auto callbackType = context->getRuntime()->getCallbackType(eventParameterTypes, context->getRuntime()->getVoidType());

            if (!callbackType) {
                context->getCompiler()->reportInternalError("Failed to get callback type for event '" + eventName + "'");
            }

            const auto functionId = parseCallbackParameter(context, function, tokens, callbackType->asCallback());

            if (functionId == NOMAD_INVALID_ID) {
                return false;
            }

            auto eventCallbackArgument = createEventCallbackArgument(
                tokens->getLineIndex(),
                tokens->getColumnIndex(),
                context->getRuntime()->getEventCallbackType(),
                eventId,
                functionId
            );

            arguments->add(std::move(eventCallbackArgument));

        } else if (const auto callbackType = argumentType->asCallback(); callbackType != nullptr) {
            if (tokens->is("then") && i + 1 < nativeFunction.parameters.size()) {
                context->reportError("`then` callback must be the last argument", tokens);

                return false;
            }

            auto functionId = parseCallbackParameter(context, function, tokens, callbackType);

            if (functionId == NOMAD_INVALID_ID) {
                return false;
            }

            auto argument = createCallbackArgument(
                tokens->getLineIndex(),
                tokens->getColumnIndex(),
                argumentType,
                functionId
            );

            arguments->add(std::move(argument));
        } else if (argumentType == context->getRuntime()->getPredicateType()) {
            auto argument = parsePredicateArgument(context, function, tokens);

            if (argument == nullptr) {
                return false;
            }

            arguments->add(std::move(argument));
        } else {
            auto argument = parseExpressionArgument(context, argumentType, function, tokens);

            if (argument == nullptr) {
                return false;
            }

            arguments->add(std::move(argument));
        }
    }

    return true;
}

std::unique_ptr<Argument> parseExpressionArgument(
    CompilerContext* context,
    const Type* argumentType,
    Function* function,
    Tokenizer* tokens,
    const ParameterAccess parameterAccess
) {
    auto expression = parseExpression(context, function, tokens);

    if (expression == nullptr) {
        return nullptr;
    }

    return createExpressionArgument(
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        argumentType,
        std::move(expression),
        parameterAccess
    );
}

std::unique_ptr<Argument> parsePredicateArgument(
    CompilerContext* context,
    Function* function,
    Tokenizer* tokens
) {
    const auto predicateFunctionName = context->getCompiler()->generateFunctionName("predicate", function, tokens->getLineIndex());

    const auto predicateFunctionId = context->getCompiler()->registerFunctionSource(
        predicateFunctionName,
        function->getPath(),
        tokens->getLine()
    );

    auto predicateExpression = parser::parseExpression(context, function, tokens);

    if (predicateExpression == nullptr) {
        return nullptr;
    }

    // Wrap predicate in return statement.
    auto returnStatement = std::make_unique<ReturnStatementNode>(tokens->getLineIndex(), tokens->getColumnIndex(), std::move(predicateExpression));

    // Wrap statement in statement list.
    auto functionNode = std::make_unique<FunctionNode>(tokens->getLineIndex(), tokens->getColumnIndex());
    functionNode->addStatement(std::move(returnStatement));

    // Assign statement list to function
    context->getCompiler()->setFunctionNode(predicateFunctionId, std::move(functionNode));

    auto predicateArgument = createPredicateArgument(
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        predicateFunctionId
    );

    return predicateArgument;
}

} // namespace parser

} // namespace nomad
