// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/compiler/Source.hpp>

#include <nomad/script/InterpreterContext.hpp>

namespace nomad {

class Interpreter {
public:
    [[nodiscard]] NomadBoolean execute(const NomadString& source, InterpreterContext& context) const;
    [[nodiscard]] NomadBoolean execute(const SourceText& source, InterpreterContext& context) const;
    // Requires one non-void expression. Syntax/type errors are retained; execution faults propagate.
    [[nodiscard]] bool evaluateExpression(const NomadString& source, InterpreterContext& context) const;
    [[nodiscard]] bool evaluateExpression(const SourceText& source, InterpreterContext& context) const;
};

} // namespace nomad
