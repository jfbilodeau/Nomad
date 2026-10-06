// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Interpreter.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>
#include <nomad/compiler/SyntaxTree.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Function.hpp>
#include <nomad/script/Type.hpp>

#include <algorithm>
#include <stdexcept>

namespace nomad {

namespace {

// Name and path of the console function; identifies console input in diagnostics.
constexpr auto CONSOLE_SOURCE_NAME = "<console>";

} // namespace

TypedValue::TypedValue(const Type* type, const RuntimeValue& value):
    m_type(type)
{
    if (m_type == nullptr) {
        throw std::invalid_argument("VirtualMachine value type cannot be null");
    }

    if (m_type->isVoid()) {
        return;
    }

    m_type->initValue(m_value);

    try {
        m_type->copyValue(value, m_value);
    } catch (...) {
        m_type->freeValue(m_value);
        m_type = nullptr;
        throw;
    }
}

TypedValue::TypedValue(const TypedValue& other):
    TypedValue(other.m_type, other.m_value)
{
}

TypedValue::TypedValue(TypedValue&& other) noexcept:
    m_type(other.m_type),
    m_value(other.m_value)
{
    other.m_type = nullptr;
}

TypedValue& TypedValue::operator=(const TypedValue& other) {
    if (this != &other) {
        TypedValue copy(other);
        std::swap(m_type, copy.m_type);
        std::swap(m_value, copy.m_value);
    }

    return *this;
}

TypedValue& TypedValue::operator=(TypedValue&& other) noexcept {
    if (this != &other) {
        release();
        m_type = other.m_type;
        m_value = other.m_value;
        other.m_type = nullptr;
    }

    return *this;
}

TypedValue::~TypedValue() {
    release();
}

const Type* TypedValue::getType() const {
    return m_type;
}

const RuntimeValue& TypedValue::getValue() const {
    return m_value;
}

void TypedValue::release() noexcept {
    if (m_type != nullptr && !m_type->isVoid()) {
        m_type->freeValue(m_value);
    }
    m_type = nullptr;
}

void Interpreter::setVariable(
    const NomadString& name,
    const Type* type,
    const RuntimeValue& value
) {
    if (name.empty()) {
        throw std::invalid_argument("Interpreter variable name cannot be empty");
    }
    if (type == nullptr || type->isVoid()) {
        throw std::invalid_argument("Interpreter variables must have a non-void type");
    }

    auto replacement = std::make_unique<TypedValue>(type, value);

    auto variableId = m_function->getVariableId(name);
    if (variableId == NOMAD_INVALID_ID) {
        variableId = m_function->registerVariable(name, type);
    } else {
        // Unlike compiled functions, console variables may change type on reassignment.
        m_function->setVariableType(variableId, type);
    }

    const auto index = toNomadIndex(variableId);
    if (index >= m_variables.size()) {
        m_variables.resize(index + 1);
    }
    m_variables[index] = std::move(replacement);
}

const Type* Interpreter::getVariableType(const NomadString& name) const {
    return getVariableTypeById(m_function->getVariableId(name));
}

const Type* Interpreter::getVariableTypeById(const NomadId variableId) const {
    if (variableId == NOMAD_INVALID_ID) {
        return nullptr;
    }

    const auto index = toNomadIndex(variableId);
    if (index >= m_variables.size()) {
        return nullptr;
    }

    const auto& variable = m_variables[index];
    return variable != nullptr ? variable->getType() : nullptr;
}

const RuntimeValue* Interpreter::getVariableValue(const NomadString& name) const {
    return getVariableValueById(m_function->getVariableId(name));
}

const RuntimeValue* Interpreter::getVariableValueById(const NomadId variableId) const {
    if (variableId == NOMAD_INVALID_ID) {
        return nullptr;
    }

    const auto index = toNomadIndex(variableId);
    if (index >= m_variables.size()) {
        return nullptr;
    }

    const auto& variable = m_variables[index];
    return variable != nullptr ? &variable->getValue() : nullptr;
}

std::vector<InterpreterVariableInfo> Interpreter::listVariables() const {
    std::vector<InterpreterVariableInfo> variables;
    variables.reserve(m_variables.size());

    for (NomadIndex variableIndex = 0; variableIndex < m_variables.size(); ++variableIndex) {
        const auto& variable = m_variables[variableIndex];
        if (variable == nullptr) {
            continue;
        }

        const auto variableId = toNomadId(variableIndex);
        variables.push_back({
            m_function->getVariableName(variableId),
            variable->getType(),
            variable->getType()->toString(variable->getValue())
        });
    }

    std::ranges::sort(variables, {}, &InterpreterVariableInfo::name);

    return variables;
}

Interpreter::Interpreter(Runtime* runtime):
    m_runtime(runtime),
    m_function(std::make_unique<Function>(NOMAD_INVALID_ID, CONSOLE_SOURCE_NAME, CONSOLE_SOURCE_NAME, ""))
{
}

Interpreter::~Interpreter() = default;

const Function* Interpreter::getFunction() const {
    return m_function.get();
}

Runtime* Interpreter::getRuntime() const {
    if (m_runtime == nullptr) {
        throw std::logic_error("Interpreter requires a Runtime for evaluation");
    }

    return m_runtime;
}

VirtualMachine* Interpreter::getVirtualMachine() {
    if (m_virtualMachine == nullptr) {
        m_virtualMachine = getRuntime()->createVirtualMachine();
    }

    return m_virtualMachine.get();
}

void Interpreter::setResult(const Type* type, const RuntimeValue& value) {
    m_result.emplace(type, value);
}

void Interpreter::setResult(const TypedValue& value) {
    m_result.emplace(value);
}

void Interpreter::clearResult() {
    m_result.reset();
}

void Interpreter::setVoidResult() {
    setResult(getRuntime()->getVoidType(), RuntimeValue{});
}

const TypedValue* Interpreter::getResult() const {
    return m_result ? &*m_result : nullptr;
}

void Interpreter::setError(const NomadString& error) {
    if (!m_error) {
        m_error = error;
    }
}

void Interpreter::clearError() {
    m_error.reset();
}

NomadBoolean Interpreter::hasError() const {
    return m_error.has_value();
}

const NomadString* Interpreter::getError() const {
    return m_error ? &*m_error : nullptr;
}

NomadBoolean Interpreter::execute(const NomadString& source) {
    clearError();
    clearResult();

    if (m_runtime == nullptr) {
        setError("Interpreter requires a Runtime to execute input");
        return false;
    }

    try {
        auto compiler = m_runtime->createCompiler();
        CompilerContext compilerContext(compiler.get(), CompilerMode::Interpreter);
        Tokenizer tokenizer(&compilerContext, CONSOLE_SOURCE_NAME, source);
        if (tokenizer.getLineCount() > 1) {
            setError("Interpreter accepts a single line of input");
            return false;
        }
        if (const auto* lexingError = compilerContext.getFirstError(); lexingError != nullptr) {
            setError(lexingError->message);
            return false;
        }
        if (tokenizer.endOfLine()) {
            setVoidResult();
            return true;
        }

        const auto& firstToken = tokenizer.currentToken();
        const auto isAssignment = tokenizer.getTokenCount() >= 3 &&
                                  tokenizer.getTokenAt(1) == "=";
        const auto isStatement = firstToken.type == TokenType::Identifier &&
                                 compiler->isStatement(firstToken.textValue);
        NativeFunctionDefinition nativeFunction;
        const auto isNativeFunction = firstToken.type == TokenType::Identifier &&
                               m_runtime->getNativeFunctionDefinition(firstToken.textValue, nativeFunction);
        const auto functionId = firstToken.type == TokenType::Identifier
            ? m_runtime->getFunctionId(firstToken.textValue)
            : NOMAD_INVALID_ID;
        const auto* function = functionId == NOMAD_INVALID_ID ? nullptr : m_runtime->getFunction(functionId);
        const auto isValueReturningCall =
            (isNativeFunction && nativeFunction.returnType != nullptr && !nativeFunction.returnType->isVoid()) ||
            (function != nullptr && function->getReturnType() != nullptr && !function->getReturnType()->isVoid());
        const auto line = tokenizer.getLineIndex();
        const auto column = tokenizer.getColumnIndex();
        const auto parseAsExpression = !isAssignment && !isStatement &&
                                       (isValueReturningCall || (!isNativeFunction && function == nullptr));

        std::unique_ptr<Statement> statement;
        if (parseAsExpression) {
            auto expression = parser::parseExpression(&compilerContext, m_function.get(), &tokenizer);
            if (expression != nullptr &&
                parser::expectEndOfLine(&compilerContext, m_function.get(), &tokenizer)) {
                statement = std::make_unique<ExpressionStatement>(
                    line,
                    column,
                    std::move(expression)
                );
            }
        } else if (isStatement && !isAssignment) {
            setError("Statement '" + firstToken.textValue + "' is not supported by the interpreter");
            return false;
        } else {
            statement = parser::parseLine(&compilerContext, m_function.get(), &tokenizer);
        }

        if (const auto* parseError = compilerContext.getFirstError(); parseError != nullptr) {
            setError(parseError->message);
            return false;
        }

        if (statement == nullptr) {
            setVoidResult();
            return true;
        }

        if (!statement->resolve(&compilerContext, m_function.get())) {
            const auto* firstError = compilerContext.getFirstError();
            setError(firstError != nullptr ? firstError->message : "Failed to resolve interpreter input");
            return false;
        }

        statement->evaluate(*this);
        return !hasError();
    } catch (const NomadException& exception) {
        clearResult();
        setError(exception.what());
        return false;
    } catch (const std::exception& exception) {
        clearResult();
        setError(exception.what());
        return false;
    }
}

} // namespace nomad
