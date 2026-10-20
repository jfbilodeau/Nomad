// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/LoopStatement.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/InterpreterContext.hpp>
#include <nomad/script/Runtime.hpp>

namespace nomad {

namespace {

class InterpreterLoopVariable {
public:
    InterpreterLoopVariable(InterpreterContext& context, const NomadString& name):
        m_context(context), m_name(name) {}
    InterpreterLoopVariable(const InterpreterLoopVariable&) = delete;
    InterpreterLoopVariable& operator=(const InterpreterLoopVariable&) = delete;
    ~InterpreterLoopVariable() { m_context.clearVariableValue(m_name); }

private:
    InterpreterContext& m_context;
    NomadString m_name;
};

class LoopControlStatement final : public Statement {
public:
    LoopControlStatement(NomadIndex line, NomadIndex column, LoopStatement* loop, bool isBreak):
        Statement(line, column), m_loop(loop), m_isBreak(isBreak) {}

protected:
    void onCompile(Compiler* compiler, ScriptFunction*) override {
        m_loop->compileControl(compiler, m_isBreak);
    }
    void onEvaluate(InterpreterContext& context) const override {
        context.setLoopControl(m_isBreak ? LoopControl::Break : LoopControl::Continue);
    }

private:
    LoopStatement* m_loop;
    bool m_isBreak;
};

class IterableForStatement final : public LoopStatement {
public:
    IterableForStatement(
        NomadIndex line,
        NomadIndex column,
        NomadString binding,
        std::unique_ptr<Expression> source
    ):
        LoopStatement(line, column, false, false),
        m_binding(std::move(binding)),
        m_source(std::move(source))
    {}

protected:
    void onResolve(CompilerContext* context, ScriptFunction* function) override {
        auto* runtime = context->getRuntime();
        if (m_factory == nullptr) {
            if (!m_source->resolve(context, function)) {
                return;
            }
            const auto iterable = runtime->getIterableDefinition(m_source->getType());
            if (!iterable) {
                reportError(context, function, "For source must be an iterable object");
                return;
            }
            m_valueType = iterable->valueType;
            m_iteratorType = iterable->iteratorType;
            const auto* factory = runtime->getMethod(iterable->factoryId);
            m_factory = member(factory->getName(), std::move(m_source));
        }
        if (!m_factory->resolve(context, function)) {
            return;
        }
        m_valueId = function->registerVariable(m_binding, m_valueType);
        m_iteratorId = function->registerVariable(m_binding + "@iterator", m_iteratorType);
        if (m_advance == nullptr) {
            m_advance = member("moveNext", iteratorVariable());
            m_current = member("current", iteratorVariable());
        }
        (void)m_advance->resolve(context, function);
        (void)m_current->resolve(context, function);
        (void)m_body.resolve(context, function);
    }

    void onCompile(Compiler* compiler, ScriptFunction* function) override {
        resetControls();
        const auto store = [&](Expression* expression, const Type* type, NomadId id) {
            expression->compile(compiler, function, ExpressionTarget::Result);
            compiler->addFreeValue(type->getTypeOpCodes()->freeFunctionVariable, toNomadIndex(id));
            compiler->addOpCode(type->getTypeOpCodes()->setFunctionVariable);
            compiler->addId(id);
        };
        store(m_factory.get(), m_iteratorType, m_iteratorId);
        const auto continuation = compiler->getOpCodeSize();
        m_advance->compile(compiler, function, ExpressionTarget::Result);
        compiler->addOpCode(OpCodes::op_jump_if_false);
        const auto exit = compiler->addIndex(NOMAD_INVALID_INDEX);
        store(m_current.get(), m_valueType, m_valueId);
        m_body.compile(compiler, function);
        compiler->addOpCode(OpCodes::op_jump);
        compiler->addIndex(continuation);
        const auto cleanup = compiler->getOpCodeSize();
        compiler->setIndex(exit, cleanup);
        patchControls(compiler, continuation, cleanup);
        compiler->addFreeValue(m_valueType->getTypeOpCodes()->freeFunctionVariable, toNomadIndex(m_valueId));
        compiler->addFreeValue(m_iteratorType->getTypeOpCodes()->freeFunctionVariable, toNomadIndex(m_iteratorId));
    }

    void onEvaluate(InterpreterContext& context) const override {
        const InterpreterLoopVariable binding(context, m_binding);
        const InterpreterLoopVariable iterator(context, m_binding + "@iterator");
        m_factory->evaluate(context);
        if (context.hasError()) {
            return;
        }
        context.setVariable(m_binding + "@iterator", m_iteratorType, context.getResult()->getValue());
        while (!context.hasError()) {
            m_advance->evaluate(context);
            if (context.hasError() || !context.getResult()->getValue().getBooleanValue()) {
                break;
            }
            m_current->evaluate(context);
            if (context.hasError()) {
                break;
            }
            context.setVariable(m_binding, m_valueType, context.getResult()->getValue());
            m_body.evaluate(context);
            const auto control = context.getLoopControl();
            context.setLoopControl(LoopControl::None);
            if (control == LoopControl::Break) {
                break;
            }
        }
        context.setVoidResult();
    }

private:
    std::unique_ptr<Expression> iteratorVariable() const {
        return std::make_unique<IdentifierExpression>(nullptr, getLine(), getColumn(), m_binding + "@iterator");
    }
    std::unique_ptr<Expression> member(const NomadString& name, std::unique_ptr<Expression> receiver) const {
        return std::make_unique<NativeMemberExpression>(
            nullptr, getLine(), getColumn(), name, name, std::move(receiver), ArgumentList{}
        );
    }

    NomadString m_binding;
    std::unique_ptr<Expression> m_source;
    std::unique_ptr<Expression> m_factory;
    std::unique_ptr<Expression> m_advance;
    std::unique_ptr<Expression> m_current;
    const Type* m_valueType = nullptr;
    const Type* m_iteratorType = nullptr;
    NomadId m_valueId = NOMAD_INVALID_ID;
    NomadId m_iteratorId = NOMAD_INVALID_ID;
};

std::unique_ptr<Statement> createControl(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    bool isBreak
) {
    auto* loop = context->getEnclosingLoop(function);
    if (loop == nullptr) {
        context->reportError("Loop control requires an enclosing loop in the same function", tokens);
        return nullptr;
    }
    return std::make_unique<LoopControlStatement>(tokens->getLineIndex(), tokens->getColumnIndex(), loop, isBreak);
}

std::unique_ptr<Statement> parseLoop(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens,
    bool postTest,
    bool until
) {
    auto loop = std::make_unique<LoopStatement>(tokens->getLineIndex(), tokens->getColumnIndex(), postTest, until);
    if (!postTest) {
        auto condition = parser::parseExpression(context, function, tokens);
        if (!condition) {
            return nullptr;
        }
        loop->setCondition(std::move(condition));
    }
    if (!parser::expectEndOfLine(context, function, tokens)) {
        return nullptr;
    }
    tokens->nextLine();
    const ParserBlockScope block(*context, function, loop.get(), false, context->currentBodyEndsAtEnd());
    if (!parser::parseBlock(context, function, tokens, {"end"}, &loop->getBody())) {
        return nullptr;
    }
    tokens->next();
    if (postTest) {
        const auto trailingUntil = tokens->is("until");
        if (!trailingUntil && !tokens->is("while")) {
            context->reportError("Expected 'while' or 'until' after 'end' of a 'do' loop", tokens);
            return nullptr;
        }
        loop->setUntil(trailingUntil);
        tokens->next();
        auto condition = parser::parseExpression(context, function, tokens);
        if (!condition) {
            return nullptr;
        }
        loop->setCondition(std::move(condition));
    }
    if (!parser::expectEndOfLine(context, function, tokens)) {
        return nullptr;
    }
    return loop;
}

} // namespace

LoopStatement::LoopStatement(NomadIndex line, NomadIndex column, bool postTest, bool until):
    Statement(line, column), m_postTest(postTest), m_until(until) {}

StatementList& LoopStatement::getBody() { return m_body; }
void LoopStatement::setCondition(std::unique_ptr<Expression> condition) { m_condition = std::move(condition); }
void LoopStatement::setUntil(bool until) { m_until = until; }

void LoopStatement::checkReachability(CompilerContext* context, const ScriptFunction* function) const {
    m_body.checkReachability(context, function);
}

void LoopStatement::onResolve(CompilerContext* context, ScriptFunction* function) {
    if (m_condition->resolve(context, function) && m_condition->getType() != context->getRuntime()->getBooleanType()) {
        reportError(context, function, "Loop condition must be a boolean expression");
    }
    (void)m_body.resolve(context, function);
}

void LoopStatement::compileControl(Compiler* compiler, bool isBreak) {
    compiler->addOpCode(OpCodes::op_jump);
    auto& jumps = isBreak ? m_breaks : m_continues;
    jumps.push_back(compiler->addIndex(NOMAD_INVALID_INDEX));
}

void LoopStatement::onCompile(Compiler* compiler, ScriptFunction* function) {
    resetControls();
    const auto start = compiler->getOpCodeSize();
    auto exit = NOMAD_INVALID_INDEX;
    if (!m_postTest) {
        m_condition->compile(compiler, function, ExpressionTarget::Result);
        compiler->addOpCode(m_until ? OpCodes::op_jump_if : OpCodes::op_jump_if_false);
        exit = compiler->addIndex(NOMAD_INVALID_INDEX);
    }
    m_body.compile(compiler, function);
    const auto continuation = m_postTest ? compiler->getOpCodeSize() : start;
    if (m_postTest) {
        m_condition->compile(compiler, function, ExpressionTarget::Result);
        compiler->addOpCode(m_until ? OpCodes::op_jump_if_false : OpCodes::op_jump_if);
    } else {
        compiler->addOpCode(OpCodes::op_jump);
    }
    compiler->addIndex(start);
    const auto end = compiler->getOpCodeSize();
    if (exit != NOMAD_INVALID_INDEX) {
        compiler->setIndex(exit, end);
    }
    patchControls(compiler, continuation, end);
}

void LoopStatement::resetControls() {
    m_breaks.clear();
    m_continues.clear();
}

void LoopStatement::patchControls(Compiler* compiler, NomadIndex continuation, NomadIndex end) {
    for (const auto jump : m_breaks) {
        compiler->setIndex(jump, end);
    }
    for (const auto jump : m_continues) {
        compiler->setIndex(jump, continuation);
    }
}

std::unique_ptr<Statement> parseForStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
) {
    const auto line = tokens->getLineIndex();
    const auto column = tokens->getColumnIndex();
    if (!tokens->is(TokenType::Identifier)) {
        context->reportError("Expected a new loop variable after 'for'", tokens);
        return nullptr;
    }
    const auto name = tokens->next().textValue;
    if (name.find('.') != NomadString::npos) {
        context->reportError("For loop variable must be an unqualified local name", tokens);
        return nullptr;
    }
    auto* compiler = context->getCompiler();
    if (compiler->getIdentifierType(name, function) != IdentifierType::Unknown ||
        compiler->getScopedIdentifier(name, function, line) != name) {
        context->reportError("For loop variable must be a new local name; shadowing is not supported", tokens);
        return nullptr;
    }
    if (!tokens->expect("in")) {
        return nullptr;
    }
    auto source = parser::parseExpression(context, function, tokens);
    if (!source || !parser::expectEndOfLine(context, function, tokens)) {
        return nullptr;
    }
    const auto binding = function->registerScopedVariable(name, nullptr, line);
    auto loop = std::make_unique<IterableForStatement>(line, column, binding, std::move(source));
    tokens->nextLine();
    const ParserBlockScope block(*context, function, loop.get(), false, context->currentBodyEndsAtEnd());
    const auto valid = parser::parseBlock(context, function, tokens, {"end"}, &loop->getBody());
    function->closeVariableScope(binding, tokens->getLineIndex());
    if (!valid) {
        return nullptr;
    }
    tokens->next();
    if (!parser::expectEndOfLine(context, function, tokens)) {
        return nullptr;
    }
    return loop;
}

void LoopStatement::onEvaluate(InterpreterContext& context) const {
    auto first = true;
    while (!context.hasError()) {
        if (!m_postTest || !first) {
            m_condition->evaluate(context);
            if (context.hasError() || context.getResult()->getValue().getBooleanValue() == m_until) {
                break;
            }
        }
        first = false;
        m_body.evaluate(context);
        const auto control = context.getLoopControl();
        context.setLoopControl(LoopControl::None);
        if (control == LoopControl::Break) {
            break;
        }
    }
    context.setVoidResult();
}

std::unique_ptr<Statement> parseWhileStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
) {
    return parseLoop(context, function, tokens, false, false);
}
std::unique_ptr<Statement> parseUntilStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
) {
    return parseLoop(context, function, tokens, false, true);
}
std::unique_ptr<Statement> parseDoStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
) {
    return parseLoop(context, function, tokens, true, false);
}
std::unique_ptr<Statement> parseBreakStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
) {
    return createControl(context, function, tokens, true);
}
std::unique_ptr<Statement> parseContinueStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
) {
    return createControl(context, function, tokens, false);
}
std::unique_ptr<Statement> parseLoopControlStatement(
    CompilerContext* context,
    ScriptFunction* function,
    Tokenizer* tokens
) {
    const auto isBreak = tokens->is("break");
    tokens->next();
    return createControl(context, function, tokens, isBreak);
}

} // namespace nomad
