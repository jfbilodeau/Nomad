// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/ClassMemberDeclaration.hpp>

#include <utility>

namespace nomad {

ClassMemberDeclaration::ClassMemberDeclaration(
    const NomadIndex line,
    const NomadIndex column,
    const ClassMemberKind kind,
    NomadString name,
    std::vector<Parameter> parameters,
    std::vector<ClassMemberSourceLine> sourceLines
):
    AstNode(line, column),
    m_kind(kind),
    m_name(std::move(name)),
    m_parameters(std::move(parameters)),
    m_sourceLines(std::move(sourceLines))
{}

ClassMemberKind ClassMemberDeclaration::getKind() const {
    return m_kind;
}

const NomadString& ClassMemberDeclaration::getName() const {
    return m_name;
}

const std::vector<Parameter>& ClassMemberDeclaration::getParameters() const {
    return m_parameters;
}

const std::vector<ClassMemberSourceLine>& ClassMemberDeclaration::getSourceLines() const {
    return m_sourceLines;
}

} // namespace nomad
