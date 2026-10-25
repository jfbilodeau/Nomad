// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/ClassMemberDeclaration.hpp>

#include <utility>

namespace nomad {

CallableId ClassMemberDeclaration::getCallableId() const {
    return m_callableId;
}

void ClassMemberDeclaration::setCallableId(const CallableId id) {
    m_callableId = id;
}

PropertyId ClassMemberDeclaration::getPropertyId() const {
    return m_propertyId;
}

void ClassMemberDeclaration::setPropertyId(const PropertyId id) {
    m_propertyId = id;
}

const Expression* ClassMemberDeclaration::getInitializer() const {
    return m_initializer.get();
}

Expression* ClassMemberDeclaration::getInitializer() {
    return m_initializer.get();
}

void ClassMemberDeclaration::setInitializer(std::unique_ptr<Expression> initializer) {
    m_initializer = std::move(initializer);
}

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
