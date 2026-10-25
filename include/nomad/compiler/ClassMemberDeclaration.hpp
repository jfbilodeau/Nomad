// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/Property.hpp>

namespace nomad {

enum class ClassMemberKind {
    Field = 1,
    Constructor,
    Method
};

struct ClassMemberSourceLine {
    NomadIndex line;
    std::vector<Token> tokens;
};

class ClassMemberDeclaration final : public AstNode {
public:
    ClassMemberDeclaration(
        NomadIndex line,
        NomadIndex column,
        ClassMemberKind kind,
        NomadString name,
        std::vector<Parameter> parameters,
        std::vector<ClassMemberSourceLine> sourceLines
    );

    [[nodiscard]] ClassMemberKind getKind() const;
    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] const std::vector<Parameter>& getParameters() const;
    // Field initializer tokens or callable body tokens, excluding the callable header and its closing end.
    [[nodiscard]] const std::vector<ClassMemberSourceLine>& getSourceLines() const;
    [[nodiscard]] CallableId getCallableId() const;
    void setCallableId(CallableId id);
    [[nodiscard]] const Expression* getInitializer() const;
    [[nodiscard]] Expression* getInitializer();
    void setInitializer(std::unique_ptr<Expression> initializer);
    [[nodiscard]] PropertyId getPropertyId() const;
    void setPropertyId(PropertyId id);

private:
    ClassMemberKind m_kind;
    NomadString m_name;
    std::vector<Parameter> m_parameters;
    std::vector<ClassMemberSourceLine> m_sourceLines;
    CallableId m_callableId{};
    PropertyId m_propertyId;
    std::unique_ptr<Expression> m_initializer;
};

} // namespace nomad
