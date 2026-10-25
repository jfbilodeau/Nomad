// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Callable.hpp>

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

private:
    ClassMemberKind m_kind;
    NomadString m_name;
    std::vector<Parameter> m_parameters;
    std::vector<ClassMemberSourceLine> m_sourceLines;
};

} // namespace nomad
