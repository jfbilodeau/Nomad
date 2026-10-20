// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Parser.hpp>

#include <nomad/compiler/Argument.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/compiler/FormatStringCompiler.hpp>
#include <nomad/compiler/StatementParsers.hpp>
#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/compiler/statements/ReturnStatement.hpp>

#include <nomad/system/String.hpp>

#include <algorithm>
#include <optional>

namespace nomad {

namespace parser {

namespace {

const Type* parseTypeName(
    CompilerContext* context,
    Tokenizer* tokens,
    const NomadIndex depth,
    NomadString name = {}
) {
    if (depth >= 64) {
        context->reportError("Type nesting exceeds the maximum depth of 64", tokens);
        return nullptr;
    }
    if (name.empty() && !tokens->nextIdentifier(name)) {
        return nullptr;
    }
    auto* runtime = context->getRuntime();
    const auto argumentCount = runtime->getParameterizedTypeArgumentCount(name);
    if (tokens->is("<")) {
        tokens->next();
        if (!argumentCount) {
            context->reportError("Unknown parameterized type '" + name + "'", tokens);
            return nullptr;
        }
        std::vector<const Type*> arguments;
        while (!tokens->is(">")) {
            if (tokens->endOfLine()) {
                context->reportError("Expected '>' to close type arguments for '" + name + "'", tokens);
                return nullptr;
            }
            const auto* argument = parseTypeName(context, tokens, depth + 1);
            if (argument == nullptr) {
                return nullptr;
            }
            if (argument->isVoid() || argument->isInternal()) {
                context->reportError("Invalid type argument '" + argument->getTypeName() + "' for '" + name + "'", tokens);
                return nullptr;
            }
            arguments.push_back(argument);
        }
        tokens->next();
        if (arguments.size() != *argumentCount) {
            context->reportError(
                "Parameterized type '" + name + "' expects " + std::to_string(*argumentCount) +
                " type arguments, got " + std::to_string(arguments.size()), tokens
            );
            return nullptr;
        }
        const auto* type = runtime->getParameterizedType(name, arguments);
        if (type == nullptr) {
            context->reportError("Cannot resolve parameterized type '" + name + "'", tokens);
        }
        return type;
    }
    if (argumentCount) {
        context->reportError("Type arguments required for parameterized type '" + name + "'", tokens);
        return nullptr;
    }
    const auto* type = runtime->getTypeByName(name);
    if (type == nullptr) {
        context->reportError("Unknown type '" + name + "' for parameter", tokens);
    } else if (type->isInternal()) {
        context->reportError("Internal type '" + name + "' cannot be used for parameter", tokens);
        return nullptr;
    }
    return type;
}

} // namespace

const Type* parseType(CompilerContext* context, Tokenizer* tokens) {
    return parseTypeName(context, tokens, 0);
}

using ParseExpressionFunction = std::unique_ptr<Expression> (*)(
    CompilerContext*, 
    ScriptFunction*, 
    Tokenizer*,
    const Expression*
);

namespace {

struct MemberNameParts {
    NomadString receiver;
    NomadString member;
};

std::optional<MemberNameParts> splitNativeMemberName(const Runtime* runtime, const NomadString& name) {
    const auto separator = name.rfind('.');
    if (separator == NomadString::npos || separator == 0 || separator + 1 >= name.size()) {
        return std::nullopt;
    }

    auto member = name.substr(separator + 1);
    if (!runtime->getMemberSignature(member)) {
        return std::nullopt;
    }

    return MemberNameParts{name.substr(0, separator), std::move(member)};
}

std::unique_ptr<Expression> createQualifiedReceiverExpression(CompilerContext* context, ScriptFunction* function,
                                                              const Expression* parent, const NomadString& name,
                                                              NomadIndex line, NomadIndex column);

bool parseNativeMemberArguments(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens,
                                   const NomadString& memberName, ArgumentList& arguments) {
    const auto signature = context->getRuntime()->getMemberSignature(memberName);
    if (!signature) {
        context->reportError("Unknown native member '" + memberName + "'", tokens);
        return false;
    }

    if (signature->type == MemberType::Property) {
        return true;
    }

    return parseCallableArguments(context, function, tokens, signature->parameters, &arguments);
}

std::unique_ptr<Expression> createNativeMemberExpression(
    CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent,
    NomadString qualifiedName, NomadString memberName, std::unique_ptr<Expression> receiver,
    const bool parseArguments, const NomadIndex line, const NomadIndex column) {
    ArgumentList arguments;

    if (parseArguments) {
        if (!parseNativeMemberArguments(context, function, tokens, memberName, arguments)) {
            return nullptr;
        }
    } else {
        const auto signature = context->getRuntime()->getMemberSignature(memberName);
        if (!signature) {
            context->reportError("Unknown native member '" + memberName + "'");
            return nullptr;
        }

        if (signature->type == MemberType::Method && !signature->parameters.empty()) {
            context->reportError("Native method '" + memberName +
                                 "' requires arguments before another member can be accessed");
            return nullptr;
        }
    }

    return std::make_unique<NativeMemberExpression>(parent, line, column, std::move(qualifiedName),
                                                       std::move(memberName), std::move(receiver), std::move(arguments));
}

std::unique_ptr<Expression> createQualifiedReceiverExpression(CompilerContext* context, ScriptFunction* function,
                                                              const Expression* parent, const NomadString& name,
                                                              const NomadIndex line, const NomadIndex column) {
    const auto functionId = context->getRuntime()->getCallableId(name);
    if (functionId != NOMAD_INVALID_ID) {
        const auto* targetFunction = context->getRuntime()->getFunction(functionId);
        if (targetFunction->getParameterCount() != 0) {
            context->reportError("Function receiver '" + name + "' requires arguments");
            return nullptr;
        }

        return std::make_unique<FunctionCallExpression>(parent, line, column, name);
    }

    if (context->getRuntime()->getConstantId(name) != NOMAD_INVALID_ID) {
        return std::make_unique<ConstantValueExpression>(parent, line, column, name);
    }

    const auto parts = splitNativeMemberName(context->getRuntime(), name);
    if (!parts) {
        return std::make_unique<IdentifierExpression>(
            parent, line, column, name
        );
    }

    auto receiver = createQualifiedReceiverExpression(context, function, parent, parts->receiver, line, column);
    if (!receiver) {
        return nullptr;
    }

    return createNativeMemberExpression(context, function, nullptr, parent, name, parts->member, std::move(receiver),
                                           false, line, column);
}

bool isPotentialMemberExpression(CompilerContext* context, ScriptFunction* function, const Tokenizer* tokens) {
    if (tokens->getTokenCount() == 0) {
        return false;
    }

    if (context->getCompiler()->isStatement(tokens->getTokenAt(0))) {
        return false;
    }
    if (tokens->getTokenTypeAt(0) == TokenType::Identifier) {
        const auto kind = context->getCompiler()->getIdentifierType(tokens->getTokenAt(0), function);
        if (kind == IdentifierType::NativeFunction || kind == IdentifierType::Function) {
            return false;
        }
    }

    for (NomadIndex index = 0; index < tokens->getTokenCount(); ++index) {
        if (tokens->getTokenTypeAt(index) == TokenType::Operator && tokens->getTokenAt(index) == "[") {
            return true;
        }
    }

    if (tokens->getTokenTypeAt(0) == TokenType::Identifier) {
        const auto& name = tokens->getTokenAt(0);
        if (context->getCompiler()->getIdentifierType(name, function) != IdentifierType::Unknown) {
            return false;
        }

        return splitNativeMemberName(context->getRuntime(), name).has_value();
    }

    for (NomadIndex index = 0; index < tokens->getTokenCount(); ++index) {
        if (tokens->getTokenAt(index) == ".") {
            return true;
        }
    }

    return false;
}

} // namespace

bool hasAssignmentOperator(const Tokenizer* tokens) {
    for (NomadIndex index = 0; index < tokens->getTokenCount(); ++index) {
        if (tokens->getTokenTypeAt(index) == TokenType::Operator && tokens->getTokenAt(index) == "=") {
            return true;
        }
    }
    return false;
}

bool isNativeMemberCandidate(const Compiler* compiler, const ScriptFunction* function, const NomadString& name) {
    if (!splitNativeMemberName(compiler->getRuntime(), name)) {
        return false;
    }

    // Exact qualified identifiers take precedence, including those captured from an enclosing scope.
    for (auto* scope = function; scope != nullptr;) {
        if (compiler->getIdentifierType(name, scope) != IdentifierType::Unknown) {
            return false;
        }
        scope = compiler->getRuntime()->getScriptFunction(scope->getParentId());
    }
    return compiler->getIdentifierType(name, nullptr) == IdentifierType::Unknown;
}

std::unique_ptr<Expression> parseBinaryOperatorExpression(CompilerContext* context, ScriptFunction* function,
                                                          Tokenizer* tokens, const Expression* parent,
                                                          std::vector<BinaryOperator> operators,
                                                          const ParseExpressionFunction nextParseFn) {
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

std::unique_ptr<Statement> parseLine(CompilerContext* context, ScriptFunction* function, Tokenizer* tokenizer) {
    if (!tokenizer->isReportingTo(context)) {
        context->getCompiler()->reportInternalError("Parser and tokenizer must share the same compiler context");
    }

    if (tokenizer->endOfLine()) {
        // Empty line
        return nullptr;
    }

    if (hasAssignmentOperator(tokenizer) && !context->getCompiler()->isStatement(tokenizer->getTokenAt(0))) {
        return parseAssignmentStatement(context, function, tokenizer);
    }

    if (isPotentialMemberExpression(context, function, tokenizer)) {
        const auto statementLine = tokenizer->getLineIndex();
        auto expression = parseExpression(context, function, tokenizer);
        if (!expression || !expectEndOfLine(context, function, tokenizer, statementLine)) {
            return nullptr;
        }

        if (auto* member = expression->asNativeExpression()) {
            member->setAllowVoid(true);
        }

        return std::make_unique<ExpressionStatement>(expression->getLine(), expression->getColumn(),
                                                     std::move(expression));
    }

    return parseStatement(context, function, tokenizer);
}

bool parseBlock(
    CompilerContext* context,
    ScriptFunction* function,
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

        const auto statementLine = tokens->getLineIndex();
        auto statement = parseLine(context, function, tokens);

        if (statement != nullptr) {
            statements->addStatement(std::move(statement));
        }

        if (!tokens->endOfLine() && tokens->getLineIndex() != statementLine &&
            std::ranges::find(endTokens, tokens->currentToken().textValue) != endTokens.end()) {
            return context->getErrorCount() == errorCount;
        }
        tokens->nextLine();
    }
}

std::unique_ptr<Statement> parseStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
) {
    auto& statementToken = tokens->next();
    const auto statementName = statementToken.textValue;

    ParseStatementFn statementFn;

    if (context->getCompiler()->getParseStatementFn(statementName, statementFn)) {
        if (context->getMode() == CompilerMode::Interpreter &&
            (statementName == "const" || statementName == "enum" || statementName == "fun" ||
                statementName == "event" || statementName == "params" || statementName == "return" ||
                statementName == "assert")) {
            context->reportError("Statement '" + statementName + "' is not supported by the interpreter", tokens);
            return nullptr;
        }
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

    const auto functionId = context->getRuntime()->getCallableId(statementName);

    if (functionId != NOMAD_INVALID_ID) {
        const auto callLine = tokens->getLineIndex();
        auto statement = parseFunctionCall(context, function, tokens, statementName);

        if (statement == nullptr || !expectEndOfLine(context, function, tokens, callLine)) {
            return nullptr;
        }

        return statement;
    }

    context->reportError("Unknown statement, native function or function name: '" + statementName + "'", tokens);

    return nullptr;
}

NomadId parseCallbackParameter(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    const FunctionType* callbackType)
{
    const auto isFun = tokens->is("fun");

    if (isFun || tokens->is("then")) {
        if (!isFun && !context->isFunctionRoot(function)) {
            context->reportError(
                "`then` is only allowed at the root of a script or function body. "
                "Use an explicit `fun ... end` callback inside control-flow blocks.", tokens
            );
            return NOMAD_INVALID_ID;
        }
        if (context->getMode() != CompilerMode::Script) {
            context->reportError(
                NomadString("Inline `") + (isFun ? "fun" : "then") + "` callbacks are not supported by the interpreter"
            , tokens);

            return NOMAD_INVALID_ID;
        }

        tokens->next();

        const auto functionName = generateFunFunctionName(context, function, tokens->getLineIndex());
        auto* compiler = context->getCompiler();
        const auto functionId = compiler->createFunction(context, compiler->getScriptId(function->getId()), functionName);
        if (functionId == NOMAD_INVALID_ID) {
            return NOMAD_INVALID_ID;
        }

        const auto callbackFunction = context->getRuntime()->getScriptFunction(functionId);

        // Wire the nested function's parent reference.
        callbackFunction->setParentId(function->getId());

        const auto declarationLine = tokens->getLineIndex();
        auto parametersParsed = parseCallbackFunParameters(context, callbackFunction, tokens, callbackType);

        if (parametersParsed) {
            parametersParsed = tokens->expectEndOfLine();
        } else {
            // Still parse the body so its lines are not reported as top-level statements.
            tokens->nextLine();
        }

        auto callbackBody = isFun
            ? parseFunBody(context, callbackFunction, tokens, declarationLine)
            : parseThenBody(context, callbackFunction, tokens, declarationLine);

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

    if (context->getRuntime()->getScriptFunction(funId)->getParameterCount() != callbackType->getParameterCount()) {
        context->reportError("Callback function '" + funName + "' has incorrect number of parameters", tokens);

        return NOMAD_INVALID_ID;
    }

    for (auto i = NomadIndex{0}; i < callbackType->getParameterCount(); ++i) {
        const auto parameterId = toNomadId(i);
        const auto expectedType = callbackType->getParameterType(parameterId);
        const auto actualType = context->getRuntime()->getScriptFunction(funId)->getParameterType(parameterId);
        if (expectedType != actualType) {
            context->reportError("Callback function '" + funName + "' parameter type mismatch at index " + toString(i), tokens);

            return NOMAD_INVALID_ID;
        }
    }

    return funId;
}

std::unique_ptr<Statement> parseFunctionCall(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    const NomadString& functionName
) {
    auto functionCallStatement = std::make_unique<FunctionCallStatementNode>(
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        functionName
    );

    const auto functionId = context->getRuntime()->getCallableId(functionName);

    if (functionId == NOMAD_INVALID_ID) {
        context->reportError("Unknown function name: '" + functionName + "'", tokens);

        return nullptr;
    }

    const auto* targetFunction = context->getRuntime()->getFunction(functionId);

    if (!parseCallableArguments(context, function, tokens, *targetFunction, functionCallStatement->getArguments())) {
        return nullptr;
    }

    return functionCallStatement;
}

std::unique_ptr<Statement> parseReturnStatement(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens) {
    auto expression = parseExpression(context, function, tokens);

    if (expression == nullptr) {
        return nullptr;
    }

    return std::make_unique<ReturnStatementNode>(tokens->getLineIndex(), tokens->getColumnIndex(), std::move(expression));
}

std::unique_ptr<Expression> parseExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens) {
    return parseLogicalAndExpression(context, function, tokens, nullptr);
}

std::unique_ptr<Expression> parseLogicalAndExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent) {
    return parseBinaryOperatorExpression(
        context,
        function,
        tokens,
        parent,
        {BinaryOperator::AndAnd},
        &parseLogicalOrExpression
    );
}


std::unique_ptr<Expression> parseLogicalOrExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent) {
    return parseBinaryOperatorExpression(
        context,
        function,
        tokens,
        parent,
        {BinaryOperator::PipePipe},
        &parseRelationalExpression
    );
}

std::unique_ptr<Expression> parseRelationalExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent) {
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

std::unique_ptr<Expression> parseBitwiseAndExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent) {
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

std::unique_ptr<Expression> parseBitwiseXorExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent) {
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

std::unique_ptr<Expression> parseBitwiseOrExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent) {
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

std::unique_ptr<Expression> parseTermExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent) {
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

std::unique_ptr<Expression> parseProductExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens,
                                                   const Expression* parent) {
    return parseBinaryOperatorExpression(context, function, tokens, parent,
                                         {
                                             BinaryOperator::Star,
                                             BinaryOperator::Slash,
                                             BinaryOperator::Percent,
                                         },
                                         &parseUnaryOperatorExpression);
}

std::unique_ptr<Expression> parseParenthesesExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens,
                                                       const Expression* parent) {
    return parsePostfixExpression(context, function, tokens, parent);
}

std::unique_ptr<Expression> parseUnaryOperatorExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent) {
    if (tokens->endOfLine()) {
        context->reportError("Unexpected end of line", tokens);

        return nullptr;
    }

    auto token = tokens->currentToken();

    if (token.type == TokenType::Operator && token.textValue == "$") {
        const auto line = tokens->getLineIndex();
        tokens->next();
        auto expression = parseUnaryOperatorExpression(context, function, tokens, parent);
        if (expression == nullptr) {
            return nullptr;
        }
        return std::make_unique<DynamicFormatExpression>(parent, line, token.column, std::move(expression));
    }

    auto op = getUnaryOperator(token.textValue);

    if (op == UnaryOperator::Unknown) {
        return parsePostfixExpression(context, function, tokens, parent);
    }

    tokens->next();

    auto expression = parseUnaryOperatorExpression(context, function, tokens, parent);
    if (expression == nullptr) {
        return nullptr;
    }

    return std::make_unique<UnaryExpression>(parent, tokens->getLineIndex(), tokens->getColumnIndex(), op,
                                             std::move(expression));
}

namespace {

// True when the current token can begin an argument; false at the end of the line or at a closing/binary operator.
bool isArgumentStart(const Tokenizer* tokens) {
    if (tokens->endOfLine()) {
        return false;
    }
    if (!tokens->is(TokenType::Operator)) {
        return true;
    }
    if (tokens->is(")") || tokens->is("]")) {
        return false;
    }
    return tokens->is("(") || tokens->is("$") ||
           getUnaryOperator(tokens->currentToken().textValue) != UnaryOperator::Unknown;
}

} // namespace

std::unique_ptr<Expression> parseNewExpression(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    const Expression* parent
) {
    const auto line = tokens->getLineIndex();
    const auto column = tokens->getColumnIndex();
    if (!tokens->expect("new")) {
        return nullptr;
    }
    if (tokens->endOfLine()) {
        context->reportError("Expected constructor name after 'new'", tokens);
        return nullptr;
    }
    auto* runtime = context->getRuntime();
    NomadString constructorName = tokens->currentToken().textValue;
    if (const auto count = runtime->getParameterizedConstructorArgumentCount(constructorName)) {
        tokens->next();
        if (!tokens->expect("<")) {
            return nullptr;
        }
        std::vector<const Type*> typeArguments;
        while (!tokens->endOfLine() && !tokens->is(">")) {
            const auto* argument = parseType(context, tokens);
            if (argument == nullptr) {
                return nullptr;
            }
            typeArguments.push_back(argument);
        }
        if (!tokens->expect(">")) {
            return nullptr;
        }
        if (typeArguments.size() != *count) {
            context->reportError("Incorrect number of constructor type arguments for '" + constructorName + "'", tokens);
            return nullptr;
        }
        constructorName = runtime->getParameterizedConstructorName(constructorName, typeArguments);
        if (constructorName.empty()) {
            context->reportError("Cannot resolve parameterized constructor", tokens);
            return nullptr;
        }
    } else if (runtime->getParameterizedTypeArgumentCount(constructorName)) {
        tokens->next();
        if (!tokens->is("<")) {
            const auto signature = runtime->getConstructorTemplate(constructorName);
            if (!signature) {
                context->reportError("Constructor '" + constructorName +
                    "' requires explicit type arguments: no inference signature is registered", tokens);
                return nullptr;
            }
            std::vector<Parameter> parameters;
            parameters.reserve(signature->size());
            for (const auto& parameter : *signature) {
                const auto* concreteType = std::get_if<const Type*>(&parameter.type);
                parameters.push_back(defParameter(
                    parameter.name, concreteType != nullptr ? *concreteType : nullptr, parameter.doc
                ));
            }
            ArgumentList arguments;
            if (!parseCallableArguments(context, function, tokens, parameters, &arguments)) {
                return nullptr;
            }
            return std::make_unique<ConstructorExpression>(
                parent, line, column, constructorName, nullptr, std::move(arguments)
            );
        }
        const auto* type = parseTypeName(context, tokens, 0, constructorName);
        if (type == nullptr) {
            return nullptr;
        }
        constructorName = type->getTypeName();
    } else if (!tokens->nextIdentifier(constructorName)) {
        return nullptr;
    }
    const NativeConstructor* shape = nullptr;
    const auto* objectType = runtime->getTypeByName(constructorName);
    for (const auto* constructor : runtime->getConstructors(objectType)) {
        if (shape == nullptr || constructor->getParameterCount() > shape->getParameterCount()) {
            shape = constructor;
        }
    }
    if (shape == nullptr) {
        context->reportError("No registered constructor named '" + constructorName + "'", tokens);
        return nullptr;
    }

    // The overload with the most parameters shapes the argument list; shorter overloads omit trailing arguments.
    ArgumentList arguments;
    if (!parseCallableArguments(context, function, tokens, *shape, &arguments, true)) {
        return nullptr;
    }
    return std::make_unique<ConstructorExpression>(
        parent, line, column, constructorName, objectType, std::move(arguments)
    );
}

std::unique_ptr<Expression> parsePostfixExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens,
                                                   const Expression* parent) {
    std::unique_ptr<Expression> expression;

    if (tokens->is(TokenType::Operator) && tokens->is("(")) {
        tokens->next();
        expression = parseExpression(context, function, tokens);

        if (expression == nullptr || !tokens->expect(")")) {
            return nullptr;
        }
    } else if ((tokens->is(TokenType::Identifier) || tokens->is(TokenType::Keyword)) && tokens->is("new")) {
        expression = parseNewExpression(context, function, tokens, parent);
        if (expression == nullptr) {
            return nullptr;
        }
    } else {
        expression = parsePrimaryExpression(context, function, tokens, parent);
        if (expression == nullptr) {
            return nullptr;
        }
    }

    while (tokens->is(TokenType::Operator) && (tokens->is(".") || tokens->is("["))) {
        if (tokens->is("[")) {
            const auto line = tokens->getLineIndex();
            const auto column = tokens->getColumnIndex();
            tokens->next();
            if (tokens->is(TokenType::Operator) && tokens->is("]")) {
                context->reportError("Indexer requires at least one index", tokens);
                return nullptr;
            }
            std::vector<std::unique_ptr<Expression>> indexes;
            while (!tokens->endOfLine() && (!tokens->is(TokenType::Operator) || !tokens->is("]"))) {
                auto index = parseExpression(context, function, tokens);
                if (index == nullptr) {
                    return nullptr;
                }
                indexes.push_back(std::move(index));
            }
            if (!tokens->is(TokenType::Operator) || !tokens->is("]")) {
                context->reportError("Expected ']' after index expressions", tokens);
                return nullptr;
            }
            tokens->next();
            expression = std::make_unique<IndexerExpression>(
                parent, line, column, std::move(expression), std::move(indexes)
            );
            continue;
        }
        tokens->next();

        NomadString memberPath;
        if (!tokens->nextIdentifier(memberPath)) {
            return nullptr;
        }

        NomadIndex start = 0;
        while (start < memberPath.size()) {
            const auto separator = memberPath.find('.', start);
            const auto isLast = separator == NomadString::npos;
            auto memberName = memberPath.substr(start, isLast ? NomadString::npos : separator - start);

            expression = createNativeMemberExpression(context, function, tokens, parent, memberName, memberName,
                                                         std::move(expression), isLast,
                                                         tokens->getLineIndex(), tokens->getColumnIndex());
            if (expression == nullptr) {
                return nullptr;
            }

            if (isLast) {
                break;
            }

            start = separator + 1;
        }
    }

    return expression;
}

bool expectEndOfLine(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    NomadIndex statementLine
) {
    if (statementLine != NOMAD_INVALID_INDEX && tokens->getLineIndex() != statementLine &&
        tokens->is("end") && tokens->getTokenCount() == 1 &&
        context->isFunctionRoot(function) && context->currentBodyEndsAtEnd()) {
        return true;
    }
    if (!tokens->endOfLine()) {
        context->reportError("Expected end of line", tokens);

        return false;
    }

    return true;
}

IdentifierType getIdentifierType(const Compiler* compiler, const NomadString& name, const ScriptFunction* function) {
    if (compiler->getRuntime()->getKeywordId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::Keyword;
    }

    if (compiler->isStatement(name)) {
        return IdentifierType::Statement;
    }

    if (compiler->getRuntime()->getNativeFunction(name) != nullptr) {
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

std::unique_ptr<Statement> parseAssignmentStatement(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens) {
    const auto line = tokens->getLineIndex();
    const auto column = tokens->getColumnIndex();

    const auto isDirectIdentifier = tokens->getTokenCount() >= 3 &&
                                    tokens->getTokenTypeAt(0) == TokenType::Identifier && tokens->getTokenAt(1) == "=";

    if (!isDirectIdentifier) {
        auto target = parseExpression(context, function, tokens);
        if (target == nullptr) {
            return nullptr;
        }

        if (!tokens->expect("=")) {
            return nullptr;
        }

        auto expression = parseExpression(context, function, tokens);
        if (expression == nullptr || !expectEndOfLine(context, function, tokens, line)) {
            return nullptr;
        }

        return std::make_unique<MemberAssignmentStatement>(line, column, std::move(target), std::move(expression));
    }

    auto variableName = tokens->currentToken().textValue;

    IdentifierDefinition identifierDefinition;
    context->getCompiler()->getIdentifierDefinition(variableName, function, identifierDefinition);

    const auto memberParts = splitNativeMemberName(context->getRuntime(), variableName);
    if (identifierDefinition.identifierType == IdentifierType::Unknown && memberParts &&
        context->getCompiler()->getIdentifierType(
            memberParts->receiver, function
        ) != IdentifierType::Unknown) {
        tokens->next();
        if (!tokens->expect("=")) {
            return nullptr;
        }

        auto receiver =
            createQualifiedReceiverExpression(context, function, nullptr, memberParts->receiver, line, column);
        if (!receiver) {
            return nullptr;
        }

        ArgumentList arguments;
        auto target = std::make_unique<NativeMemberExpression>(nullptr, line, column, variableName,
                                                                  memberParts->member, std::move(receiver),
                                                                  std::move(arguments));

        auto expression = parseExpression(context, function, tokens);
        if (expression == nullptr || !expectEndOfLine(context, function, tokens, line)) {
            return nullptr;
        }

        return std::make_unique<MemberAssignmentStatement>(line, column, std::move(target), std::move(expression));
    }

    if (identifierDefinition.identifierType == IdentifierType::Unknown && function != nullptr) {
        // Runtime context variables keep their normal registration path.
        const auto contextId = context->getRuntime()->getVariableContextIdByPrefix(variableName);

        if (contextId != NOMAD_INVALID_ID) {
            // Register new context variable
            const auto variableId = context->getRuntime()->registerOrUpdateContextVariable(variableName, nullptr);

            if (variableId == NOMAD_INVALID_ID) {
                context->getCompiler()->reportInternalError("Failed to register context variable: " + variableName);
            }
        } else if (context->getMode() == CompilerMode::Script) {
            function->registerVariable(variableName, nullptr);
        }
    }

    tokens->next(); // Consume variable name

    if (!tokens->expect("=")) {
        return nullptr;
    }

    auto expression = parseExpression(context, function, tokens);

    if (expression == nullptr || !expectEndOfLine(context, function, tokens, line)) {
        return nullptr;
    }

    return std::make_unique<AssignmentStatement>(line, column, variableName, std::move(expression));
}

std::unique_ptr<Expression> parsePrimaryExpression(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, const Expression* parent) {
    if (tokens->endOfLine()) {
        context->reportError("Unexpected end of line", tokens);

        return nullptr;
    }

    auto& token = tokens->next();

    if (token.type == TokenType::FormatString) {
        auto expression = std::make_unique<FormatStringLiteral>(
            parent,
            tokens->getLineIndex(),
            token.column,
            token.textValue
        );
        const auto offset = token.column - 1;
        const auto& sourceLine = tokens->getLine();
        const auto end = findFormatStringLiteralEnd(sourceLine, offset);
        if (!end) {
            return expression;
        }
        if (context->getMode() == CompilerMode::Script && function != nullptr) {
            context->getCompiler()->deferFormatString(expression.get(), function->getId(),
                sourceLine.substr(offset + 2, *end - offset - 2));
        }
        return expression;
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
        const auto functionId = context->getRuntime()->getCallableId(token.textValue);

        if (functionId != NOMAD_INVALID_ID) {
            return parseFunctionCallExpression(context, function, tokens, parent, token.textValue);
        }

        // Constant?
        const auto constantId = context->getRuntime()->getConstantId(token.textValue);

        if (constantId != NOMAD_INVALID_ID) {
            return std::make_unique<ConstantValueExpression>(parent, tokens->getLineIndex(), tokens->getColumnIndex(),
                                                             token.textValue);
        }

        if (isNativeMemberCandidate(context->getCompiler(), function, token.textValue)) {
            const auto memberParts = splitNativeMemberName(context->getRuntime(), token.textValue);

            if (memberParts) {
                auto receiver = createQualifiedReceiverExpression(context, function, parent, memberParts->receiver,
                                                                  tokens->getLineIndex(), tokens->getColumnIndex());
                if (!receiver) {
                    return nullptr;
                }

                return createNativeMemberExpression(
                    context, function, tokens, parent, token.textValue, memberParts->member, std::move(receiver),
                    true, tokens->getLineIndex(), tokens->getColumnIndex());
            }
        }

        if (context->getMode() != CompilerMode::Script &&
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
    ScriptFunction* function,
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

    const auto functionId = context->getRuntime()->getCallableId(functionName);
    const auto* targetFunction = context->getRuntime()->getFunction(functionId);

    if (targetFunction == nullptr) {
        context->reportError("Unknown function '" + functionName + "'", tokens);
        return nullptr;
    }

    if (!parseCallableArguments(context, function, tokens, *targetFunction, functionCallExpression->getArguments())) {
        return nullptr;
    }

    return functionCallExpression;
}

// Returns false on error.
bool parseEventDispatch(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens, std::vector<std::unique_ptr<Argument>>& arguments) {
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

bool parseCallableArguments(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    const Callable& callable,
    ArgumentList* arguments,
    const bool allowOmittedArguments
) {
    const Callable* shape = &callable;
    auto allowOmitted = allowOmittedArguments;
    auto* runtime = context->getRuntime();
    if (runtime->getFunction(callable.getId()) == &callable) {
        std::vector<NomadId> overloads;
        runtime->getCallableOverloads(callable.getName(), overloads);
        for (const auto id : overloads) {
            const auto* candidate = runtime->getFunction(id);
            if (candidate->getParameterCount() != callable.getParameterCount()) {
                allowOmitted = true;
            }
            if (candidate->getParameterCount() > shape->getParameterCount()) {
                shape = candidate;
            }
        }
    }
    return parseCallableArguments(
        context, function, tokens, shape->getParameters(), arguments, allowOmitted
    );
}

bool parseCallableArguments(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    const std::vector<Parameter>& parameters,
    ArgumentList* arguments,
    const bool allowOmittedArguments
) {
    const auto* runtime = context->getRuntime();
    auto omitRemainingArguments = false;

    for (NomadIndex i = 0; i < parameters.size(); ++i) {
        const auto& parameter = parameters[i];

        // Hidden source parameters are supplied by the compiler and consume no tokens.
        const auto isSourceParameter =
            parameter.type == runtime->getFileNameType() ||
            parameter.type == runtime->getFunctionNameType() ||
            parameter.type == runtime->getLineNumberType();

        // Overloads with fewer parameters leave the trailing script-supplied arguments out.
        if (allowOmittedArguments && !isSourceParameter &&
            (omitRemainingArguments || !isArgumentStart(tokens))) {
            omitRemainingArguments = true;
            continue;
        }

        if (!isSourceParameter && (tokens->endOfLine() || (parameter.type == nullptr && !isArgumentStart(tokens)))) {
            const auto typeName = parameter.type != nullptr ? ":" + parameter.type->getTypeName() : "";
            context->reportError("Expected arguments for `" + parameter.name + typeName + "`", tokens);

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

            std::ranges::transform(eventParameters, eventParameterTypes.begin(), [context](const auto& p) {
                if (auto type = context->getRuntime()->getType(p.typeId)) {
                    return *type;
                }

                context->getCompiler()->reportInternalError("Invalid type id in event: " + toString(p.typeId) + " (" +
                                                            p.typeName + ")");
            });

            const auto callbackType =
                context->getRuntime()->getCallbackType(eventParameterTypes, context->getRuntime()->getVoidType());

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

        } else if (const auto callbackType = argumentType != nullptr ? argumentType->asCallback() : nullptr; callbackType != nullptr) {
            if (tokens->is("then") && i + 1 < parameters.size()) {
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
    ScriptFunction* function,
    Tokenizer* tokens
) {
    auto expression = parseExpression(context, function, tokens);

    if (expression == nullptr) {
        return nullptr;
    }

    return createExpressionArgument(
        tokens->getLineIndex(),
        tokens->getColumnIndex(),
        argumentType,
        std::move(expression)
    );
}

std::unique_ptr<Argument> parsePredicateArgument(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
) {
    const auto predicateFunctionName = context->getCompiler()->generateFunctionName("predicate", function, tokens->getLineIndex());

    auto* compiler = context->getCompiler();
    const auto predicateFunctionId = compiler->createFunction(
        context, compiler->getScriptId(function->getId()), predicateFunctionName);
    if (predicateFunctionId == NOMAD_INVALID_ID) {
        return nullptr;
    }

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
