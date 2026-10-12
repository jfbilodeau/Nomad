// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Compiler.hpp>

namespace nomad {

std::unique_ptr<Statement> parseSelectStatement(CompilerContext* context, ScriptFunction* function, Tokenizer* tokens);

} // nomad
