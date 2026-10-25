// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/ClassDeclaration.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

namespace nomad {

namespace {

bool parseClassDefinition(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    NomadString& name
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
        if (tokens->is("end")) {
            tokens->next();
            return parser::expectEndOfLine(context, function, tokens);
        }
        if (!tokens->endOfLine()) {
            context->reportError("Class members are not supported yet; only empty class declarations are available", tokens);
            return false;
        }
    }
    context->reportError("Unexpected end of file in class '" + name + "': expected 'end'", tokens);
    return false;
}

bool preDeclareClass(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    const ObjectTypeMode mode
) {
    NomadString name;
    if (!parseClassDefinition(context, function, tokens, name)) {
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
    const ObjectType* type
):
    Statement(line, column),
    m_type(type)
{}

const ObjectType* ClassDeclaration::getType() const {
    return m_type;
}

NomadBoolean ClassDeclaration::isDeclaration() const {
    return true;
}

bool preParseOpenClassStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    const PreParsePhase phase
) {
    if (phase == PreParsePhase::Declarations) {
        return preDeclareClass(context, function, tokens, ObjectTypeMode::Opened);
    }
    NomadString name;
    return parseClassDefinition(context, function, tokens, name);
}

bool preParseClassStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    const PreParsePhase phase
) {
    if (phase == PreParsePhase::Declarations) {
        return preDeclareClass(context, function, tokens, ObjectTypeMode::Closed);
    }
    NomadString name;
    return parseClassDefinition(context, function, tokens, name);
}

std::unique_ptr<Statement> parseClassStatement(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens) {
    const auto line = tokens->getLineIndex();
    const auto column = tokens->getColumnIndex();
    if (context->currentBodyEndsAtEnd() || !context->isFunctionRoot(function)) {
        context->reportError("Classes may only be declared at script level or inside a hub", tokens);
        return nullptr;
    }
    NomadString name;
    if (!parseClassDefinition(context, function, tokens, name)) {
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
    auto declaration = std::make_unique<ClassDeclaration>(line, column, type->asObjectType());
    declaration->setEndSpan(tokens->getLineIndex(), tokens->getColumnIndex());
    return declaration;
}

} // namespace nomad
