// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/StructMemberDeclaration.hpp>

#include <utility>

namespace nomad {

StructMemberDeclaration::StructMemberDeclaration(
    const NomadIndex line,
    const NomadIndex column,
    const StructMemberKind kind,
    NomadString name,
    std::vector<Parameter> parameters,
    std::vector<StructMemberSourceLine> sourceLines
):
    AstNode(line, column),
    m_kind(kind),
    m_name(std::move(name)),
    m_parameters(std::move(parameters)),
    m_sourceLines(std::move(sourceLines))
{}

StructMemberKind StructMemberDeclaration::getKind() const {
    return m_kind;
}

const NomadString& StructMemberDeclaration::getName() const {
    return m_name;
}

const std::vector<Parameter>& StructMemberDeclaration::getParameters() const {
    return m_parameters;
}

const std::vector<StructMemberSourceLine>& StructMemberDeclaration::getSourceLines() const {
    return m_sourceLines;
}

CallableId StructMemberDeclaration::getCallableId() const {
    return m_callableId;
}

void StructMemberDeclaration::setCallableId(const CallableId id) {
    m_callableId = id;
}

PropertyId StructMemberDeclaration::getPropertyId() const {
    return m_propertyId;
}

void StructMemberDeclaration::setPropertyId(const PropertyId id) {
    m_propertyId = id;
}

const Expression* StructMemberDeclaration::getInitializer() const {
    return m_initializer.get();
}

Expression* StructMemberDeclaration::getInitializer() {
    return m_initializer.get();
}

void StructMemberDeclaration::setInitializer(std::unique_ptr<Expression> initializer) {
    m_initializer = std::move(initializer);
}

} // namespace nomad
