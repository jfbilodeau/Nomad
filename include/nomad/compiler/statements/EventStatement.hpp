// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include "nomad/Nomad.hpp"

#include "nomad/compiler/SyntaxTree.hpp"

#include <memory>

namespace nomad {

// Forward declaration
class Compiler;
class Expression;
class Function;
class Statement;
class SyntaxTree;
class Tokenizer;

class EventStatementNode final : public Statement {
public:
    explicit EventStatementNode(NomadIndex line, NomadIndex column, const NomadString& eventName, std::vector<NomadString> parameters);

protected:
    //Pre-parsed declaration only--nothing to compile

private:
    std::vector<NomadString> m_parameters;
};

bool preParseEventStatement(CompilerContext* context, Function* function, Tokenizer* tokens);

} // namespace nomad
