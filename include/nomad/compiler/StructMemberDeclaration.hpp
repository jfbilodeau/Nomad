// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Callable.hpp>
#include <nomad/script/Property.hpp>

namespace nomad {

enum class StructMemberKind {
    Field = 1,
    Constructor,
    Method
};

struct StructMemberSourceLine {
    NomadIndex line;
    std::vector<Token> tokens;
};

class StructMemberDeclaration final : public AstNode {
public:
    StructMemberDeclaration(
        NomadIndex line,
        NomadIndex column,
        StructMemberKind kind,
        NomadString name,
        std::vector<Parameter> parameters,
        std::vector<StructMemberSourceLine> sourceLines
    );

    [[nodiscard]] StructMemberKind getKind() const;
    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] const std::vector<Parameter>& getParameters() const;
    [[nodiscard]] const std::vector<StructMemberSourceLine>& getSourceLines() const;
    [[nodiscard]] CallableId getCallableId() const;
    void setCallableId(CallableId id);
    [[nodiscard]] const Expression* getInitializer() const;
    [[nodiscard]] Expression* getInitializer();
    void setInitializer(std::unique_ptr<Expression> initializer);
    [[nodiscard]] PropertyId getPropertyId() const;
    void setPropertyId(PropertyId id);

private:
    StructMemberKind m_kind;
    NomadString m_name;
    std::vector<Parameter> m_parameters;
    std::vector<StructMemberSourceLine> m_sourceLines;
    CallableId m_callableId{};
    PropertyId m_propertyId;
    std::unique_ptr<Expression> m_initializer;
};

} // namespace nomad
