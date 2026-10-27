// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include "nomad/Nomad.hpp"

#include "nomad/compiler/SyntaxTree.hpp"

#include <memory>

namespace nomad {

enum class PreParsePhase;

// Forward declaration
class Compiler;
class Expression;
class ScriptCallableBase;
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

bool preParseEventStatement(
    CompilerContext* context,
    ScriptCallableBase* function,
    Tokenizer* tokens,
    PreParsePhase phase
);

} // namespace nomad
