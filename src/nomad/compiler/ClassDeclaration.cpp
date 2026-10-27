// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/ClassDeclaration.hpp>

#include <nomad/compiler/ClassFieldInitializerStatement.hpp>
#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/StatementParsers.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <algorithm>
#include <utility>

namespace nomad {

namespace {

ClassMemberSourceLine takeSourceLine(Tokenizer* tokens) {
    ClassMemberSourceLine sourceLine{tokens->getLineIndex(), {}};
    while (!tokens->endOfLine()) {
        sourceLine.tokens.push_back(tokens->next());
    }
    return sourceLine;
}

bool readCallableBody(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    std::vector<ClassMemberSourceLine>& sourceLines
) {
    auto depth = NomadIndex{1};
    while (tokens->nextLine()) {
        if (tokens->is(TokenType::Identifier) && tokens->is("end")) {
            if (--depth == 0) {
                tokens->next();
                return parser::expectEndOfLine(context, function, tokens);
            }
        } else {
            depth += parser::countBlockOpenings(tokens);
        }
        sourceLines.push_back(takeSourceLine(tokens));
    }
    context->reportError("Unexpected end of file in class callable: expected 'end'", tokens);
    return false;
}

bool readMemberName(CompilerContext* context, Tokenizer* tokens, NomadString& name) {
    if (!tokens->nextIdentifier(name)) {
        return false;
    }
    if (name.find('.') != NomadString::npos || context->getRuntime()->getKeywordId(name).isValid()) {
        context->reportError("Class member names must be unqualified, non-reserved identifiers", tokens);
        return false;
    }
    return true;
}

std::unique_ptr<ClassMemberDeclaration> parseMember(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    const bool resolveParameters
) {
    const auto line = tokens->getLineIndex();
    const auto column = tokens->getColumnIndex();
    if (!tokens->expect(TokenType::Identifier)) {
        return nullptr;
    }
    auto kind = ClassMemberKind::Field;
    NomadString name;
    if (tokens->is("init")) {
        kind = ClassMemberKind::Constructor;
        name = tokens->next().textValue;
    } else {
        if (tokens->is("fun")) {
            tokens->next();
            kind = ClassMemberKind::Method;
        }
        if (!readMemberName(context, tokens, name)) {
            return nullptr;
        }
        if (kind == ClassMemberKind::Method && name == "init") {
            context->reportError("Use 'init' without 'fun' to declare a constructor", tokens);
            return nullptr;
        }
    }

    std::vector<Parameter> parameters;
    std::vector<ClassMemberSourceLine> sourceLines;
    if (kind == ClassMemberKind::Field) {
        if (!tokens->expect("=")) {
            return nullptr;
        }
        if (tokens->endOfLine()) {
            context->reportError("Class field '" + name + "' requires an initializer", tokens);
            return nullptr;
        }
        auto depth = parser::countBlockOpenings(tokens);
        sourceLines.push_back(takeSourceLine(tokens));
        while (depth > 0) {
            if (!tokens->nextLine()) {
                context->reportError("Unexpected end of file in class field initializer: expected 'end'", tokens);
                return nullptr;
            }
            if (tokens->is(TokenType::Identifier) && tokens->is("end")) {
                --depth;
            } else {
                depth += parser::countBlockOpenings(tokens);
            }
            sourceLines.push_back(takeSourceLine(tokens));
        }
    } else {
        if (resolveParameters) {
            if (!parser::parseParameters(context, tokens, parameters)) {
                return nullptr;
            }
        } else {
            while (!tokens->endOfLine()) {
                tokens->next();
            }
        }
        if (!readCallableBody(context, function, tokens, sourceLines)) {
            return nullptr;
        }
    }
    auto member = std::make_unique<ClassMemberDeclaration>(
        line, column, kind, std::move(name), std::move(parameters), std::move(sourceLines)
    );
    member->setEndSpan(tokens->getLineIndex(), tokens->getColumnIndex());
    return member;
}

bool parseClassDefinition(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    NomadString& name,
    std::vector<std::unique_ptr<ClassMemberDeclaration>>& members,
    const bool resolveParameters
) {
    if (!tokens->nextIdentifier(name)) {
        return false;
    }
    if (context->getRuntime()->getKeywordId(name).isValid()) {
        context->reportError("Class name '" + name + "' is a reserved keyword", tokens);
        return false;
    }
    if (!parser::expectEndOfLine(context, function, tokens)) {
        return false;
    }
    name = context->qualifyDeclaration(name);
    while (tokens->nextLine()) {
        if (tokens->is(TokenType::Identifier) && tokens->is("end")) {
            tokens->next();
            return parser::expectEndOfLine(context, function, tokens);
        }
        if (!tokens->endOfLine()) {
            auto member = parseMember(context, function, tokens, resolveParameters);
            if (!member) {
                return false;
            }
            if (resolveParameters) {
                for (const auto& existing : members) {
                    if (existing->getName() != member->getName()) {
                        continue;
                    }
                    if (existing->getKind() == ClassMemberKind::Field || member->getKind() == ClassMemberKind::Field ||
                        std::ranges::equal(existing->getParameters(), member->getParameters(),
                            [](const auto& first, const auto& second) { return first.type == second.type; })) {
                        context->reportError(
                            "Duplicate class member '" + member->getName() + "'",
                            tokens->getSourceName(), member->getLine(), member->getColumn()
                        );
                        return false;
                    }
                }
            }
            members.push_back(std::move(member));
        }
    }
    context->reportError("Unexpected end of file in class '" + name + "': expected 'end'", tokens);
    return false;
}

bool preDeclareClass(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    const ObjectTypeMode mode
) {
    NomadString name;
    std::vector<std::unique_ptr<ClassMemberDeclaration>> members;
    if (!parseClassDefinition(context, function, tokens, name, members, false)) {
        return false;
    }
    if (context->getCompiler()->getIdentifierType(name, nullptr) != IdentifierType::Unknown ||
        context->getRuntime()->createObjectType(name, mode).isInvalid()) {
        context->reportError("Cannot declare class '" + name + "': identifier is already declared or invalid", tokens);
        return false;
    }
    return true;
}

bool registerMemberSignatures(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens
) {
    NomadString name;
    std::vector<std::unique_ptr<ClassMemberDeclaration>> members;
    if (!parseClassDefinition(context, function, tokens, name, members, true)) {
        return false;
    }
    auto* runtime = context->getRuntime();
    const auto* type = runtime->getTypeByName(name);
    if (type == nullptr || type->asObjectType() == nullptr) {
        context->reportError("Class '" + name + "' has no registered object type", tokens);
        return false;
    }
    const auto& initializerName = type->asObjectType()->getInitializerName();
    auto initializerRegistered = std::ranges::any_of(type->asObjectType()->getMethods(), [&](const auto* method) {
        return method->getName() == initializerName;
    });
    for (const auto& member : members) {
        for (const auto& parameter : member->getParameters()) {
            if (parameter.name == "this") {
                context->reportError("'this' names the class receiver and cannot be a parameter",
                    tokens->getSourceName(), member->getLine(), member->getColumn());
                return false;
            }
        }
        CallableId id;
        if (member->getKind() == ClassMemberKind::Field) {
            if (initializerRegistered) continue;
            initializerRegistered = true;
            id = runtime->registerScriptMethod(type->getId(), initializerName, {}, tokens->getSourceName(), tokens->getSource());
        } else if (member->getKind() == ClassMemberKind::Constructor) {
            id = runtime->registerScriptConstructor(
                type->getId(), member->getParameters(), tokens->getSourceName(), tokens->getSource()
            );
        } else {
            id = runtime->registerScriptMethod(
                type->getId(), member->getName(), member->getParameters(), tokens->getSourceName(), tokens->getSource()
            );
        }
        if (id.isInvalid()) {
            context->reportError("Cannot register class member '" + member->getName() + "'",
                tokens->getSourceName(), member->getLine(), member->getColumn());
            return false;
        }
        auto* callable = runtime->getScriptCallable(id);
        callable->setHub(context->getHub());
        callable->setSourceLineRange(member->getLine(), member->getEndLine());
        const auto kind = member->getKind() == ClassMemberKind::Field ? CompilationCallableTarget::ObjectInitializer :
            member->getKind() == ClassMemberKind::Constructor ? CompilationCallableTarget::Constructor : CompilationCallableTarget::Function;
        context->getCompiler()->registerMemberCompilation(id, function->getId(), kind);
        if (member->getKind() == ClassMemberKind::Field) {
            auto body = std::make_unique<FunctionNode>(member->getLine(), member->getColumn());
            body->setEndSpan(member->getEndLine(), member->getEndColumn());
            context->getCompiler()->setFunctionBody(id, std::move(body));
        }
    }
    if (!initializerRegistered) {
        const auto id = runtime->registerScriptMethod(
            type->getId(), initializerName, {}, tokens->getSourceName(), tokens->getSource()
        );
        if (id.isInvalid()) {
            context->reportError("Cannot register class initializer", tokens);
            return false;
        }
        auto* callable = runtime->getScriptCallable(id);
        callable->setHub(context->getHub());
        context->getCompiler()->registerMemberCompilation(id, function->getId(), CompilationCallableTarget::ObjectInitializer);
        context->getCompiler()->setFunctionBody(id, std::make_unique<FunctionNode>(0, 0));
    }
    if (type->asObjectType()->getConstructors().empty()) {
        const auto id = runtime->registerScriptConstructor(
            type->getId(), {}, tokens->getSourceName(), tokens->getSource()
        );
        if (id.isInvalid()) {
            context->reportError("Cannot register default class constructor", tokens);
            return false;
        }
        runtime->getScriptCallable(id)->setHub(context->getHub());
        context->getCompiler()->registerMemberCompilation(id, function->getId(), CompilationCallableTarget::Constructor);
        context->getCompiler()->setFunctionBody(id, std::make_unique<FunctionNode>(0, 0));
    }
    return true;
}

CallableId findMemberCallable(const ObjectType* type, const ClassMemberDeclaration& member) {
    if (member.getKind() == ClassMemberKind::Constructor) {
        for (const auto* constructor : type->getConstructors()) {
            if (std::ranges::equal(constructor->getParameters(), member.getParameters(),
                [](const auto& first, const auto& second) { return first.type == second.type; })) {
                return constructor->getId();
            }
        }
    } else {
        const auto& name = member.getKind() == ClassMemberKind::Field ? type->getInitializerName() : member.getName();
        for (const auto* method : type->getMethods()) {
            if (method->getName() == name && (member.getKind() == ClassMemberKind::Field ||
                std::ranges::equal(method->getParameters(), member.getParameters(),
                    [](const auto& first, const auto& second) { return first.type == second.type; }))) {
                return method->getId();
            }
        }
    }
    return CallableId::INVALID;
}

bool parseMemberBodies(
    CompilerContext* context,
    const ObjectType* type,
    const Tokenizer* source,
    std::vector<std::unique_ptr<ClassMemberDeclaration>>& members
) {
    auto initializerBody = std::make_unique<FunctionNode>(0, 0);
    auto initializerId = CallableId::INVALID;
    for (const auto* method : type->getMethods()) {
        if (method->getName() == type->getInitializerName()) {
            initializerId = method->getId();
            break;
        }
    }
    for (auto& member : members) {
        const auto id = findMemberCallable(type, *member);
        auto* callable = context->getRuntime()->getScriptCallable(id);
        if (callable == nullptr) {
            context->reportError("Class member '" + member->getName() + "' has no registered signature",
                source->getSourceName(), member->getLine(), member->getColumn());
            return false;
        }
        member->setCallableId(id);
        Tokenizer tokens(context, source->getSourceName(), source->getSource());
        while (tokens.nextLine() && tokens.getLineIndex() < member->getLine()) {}
        if (member->getKind() == ClassMemberKind::Field) {
            tokens.next();
            if (!tokens.expect("=")) return false;
            const ParserBlockScope scope(*context, callable, nullptr, true, true);
            auto initializer = parser::parseExpression(context, callable, &tokens);
            if (!initializer || !parser::expectEndOfLine(context, callable, &tokens, member->getLine())) {
                return false;
            }
            member->setInitializer(std::move(initializer));
            initializerBody->addStatement(std::make_unique<ClassFieldInitializerStatement>(member.get()));
        } else {
            while (tokens.nextLine() && tokens.getLineIndex() < member->getEndLine()) {
                const auto statement = tokens.next().textValue;
                PreParseStatementFn preParser = nullptr;
                // Class/hub declarations are rejected by their ordinary parsers in a callable body.
                if (statement == "class" || statement == "hub" || statement == "open") continue;
                ParseStatementFn parser = nullptr;
                StatementAvailability availability;
                if (context->getCompiler()->getParseStatementFn(statement, parser, availability) &&
                    availability != StatementAvailability::Interpreter &&
                    context->getCompiler()->getPreParseStatementFn(statement, preParser) && preParser != nullptr &&
                    !preParser(context, callable, &tokens, PreParsePhase::Signatures)) {
                    return false;
                }
            }
            tokens.reset();
            while (tokens.nextLine() && tokens.getLineIndex() < member->getLine()) {}
            tokens.nextLine();
            auto body = parseFunBody(context, callable, &tokens, member->getLine());
            if (!body) return false;
            context->getCompiler()->setFunctionBody(id, std::move(body));
        }
    }
    context->getCompiler()->setFunctionBody(initializerId, std::move(initializerBody));
    return true;
}

} // namespace

ClassDeclaration::ClassDeclaration(
    const NomadIndex line,
    const NomadIndex column,
    const ObjectType* type,
    std::vector<std::unique_ptr<ClassMemberDeclaration>> members
):
    Statement(line, column),
    m_type(type),
    m_members(std::move(members))
{}

const ObjectType* ClassDeclaration::getType() const {
    return m_type;
}

const std::vector<std::unique_ptr<ClassMemberDeclaration>>& ClassDeclaration::getMembers() const {
    return m_members;
}

NomadBoolean ClassDeclaration::isDeclaration() const {
    return true;
}

void ClassDeclaration::onResolve(CompilerContext* context, ScriptCallableBase* /*function*/) {
    for (const auto& member : m_members) {
        if (auto* initializer = member->getInitializer()) {
            auto* callable = context->getRuntime()->getScriptCallable(member->getCallableId());
            if (initializer->resolve(context, callable)) {
                const auto* property = context->getCompiler()->resolveClassField(
                    context, 
                    callable, 
                    member->getName(), 
                    initializer->getType(), 
                    true,
                    member->getLine(), 
                    member->getColumn()
                );
                if (property != nullptr) {
                    member->setPropertyId(property->getId());
                    context->getRuntime()->setPropertyHasExplicitInitializer(property->getId(), true);
                }
            }
        }
    }
}

bool preParseOpenClassStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    const PreParsePhase phase
) {
    if (phase == PreParsePhase::Declarations) {
        return preDeclareClass(context, function, tokens, ObjectTypeMode::Opened);
    }
    return registerMemberSignatures(context, function, tokens);
}

bool preParseClassStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    const PreParsePhase phase
) {
    if (phase == PreParsePhase::Declarations) {
        return preDeclareClass(context, function, tokens, ObjectTypeMode::Closed);
    }
    return registerMemberSignatures(context, function, tokens);
}

std::unique_ptr<Statement> parseClassStatement(CompilerContext* context, ScriptCallableBase* function, Tokenizer* tokens) {
    const auto line = tokens->getLineIndex();
    const auto column = tokens->getColumnIndex();
    if (context->currentBodyEndsAtEnd() || !context->isFunctionRoot(function)) {
        context->reportError("Classes may only be declared at script level or inside a hub", tokens);
        return nullptr;
    }
    NomadString name;
    std::vector<std::unique_ptr<ClassMemberDeclaration>> members;
    if (!parseClassDefinition(context, function, tokens, name, members, true)) {
        return nullptr;
    }
    if (context->getCompiler()->getIdentifierType(name, nullptr) != IdentifierType::Unknown) {
        context->reportError("Cannot declare class '" + name + "': identifier is already declared", tokens);
        return nullptr;
    }
    const auto* type = context->getRuntime()->getTypeByName(name);
    if (type == nullptr || type->asObjectType() == nullptr) {
        context->reportError("Class '" + name + "' has no registered object type", tokens);
        return nullptr;
    }
    if (!parseMemberBodies(context, type->asObjectType(), tokens, members)) {
        return nullptr;
    }
    auto declaration = std::make_unique<ClassDeclaration>(line, column, type->asObjectType(), std::move(members));
    declaration->setEndSpan(tokens->getLineIndex(), tokens->getColumnIndex());
    return declaration;
}

} // namespace nomad
