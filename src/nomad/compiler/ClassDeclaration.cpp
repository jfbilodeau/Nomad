// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/ClassDeclaration.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
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

void ClassDeclaration::onResolve(CompilerContext* context, ScriptCallableBase* function) {
    if (!m_members.empty()) {
        const auto& member = m_members.front();
        context->reportError(
            "Class member execution is not supported yet; only empty class declarations can be compiled",
            function->getPath(), member->getLine(), member->getColumn()
        );
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
    NomadString name;
    std::vector<std::unique_ptr<ClassMemberDeclaration>> members;
    return parseClassDefinition(context, function, tokens, name, members, true);
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
    NomadString name;
    std::vector<std::unique_ptr<ClassMemberDeclaration>> members;
    return parseClassDefinition(context, function, tokens, name, members, true);
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
    auto declaration = std::make_unique<ClassDeclaration>(line, column, type->asObjectType(), std::move(members));
    declaration->setEndSpan(tokens->getLineIndex(), tokens->getColumnIndex());
    return declaration;
}

} // namespace nomad
