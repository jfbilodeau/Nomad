// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Interpreter.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/CompositeType.hpp>
#include <nomad/script/InterpreterCallFrame.hpp>
#include <nomad/script/NativeFunction.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

namespace nomad {

namespace {

void validateCallArguments(
    const Runtime& runtime,
    const Callable& callable,
    const std::span<const InterpreterValue> arguments
) {
    if (callable.getReturnType() == nullptr) {
        throw std::invalid_argument("Callable '" + callable.getName() + "' has an unresolved return type");
    }
    if (arguments.size() != callable.getParameterCount()) {
        throw std::invalid_argument("Incorrect argument count for callable '" + callable.getName() + "'");
    }
    for (auto index = NomadIndex{0}; index < arguments.size(); ++index) {
        const auto& parameter = callable.getParameters()[index];
        const auto* argumentType = arguments[index].getType();
        if (parameter.type == nullptr) {
            throw std::invalid_argument("Callable '" + callable.getName() + "' has an unresolved parameter type");
        }
        if (argumentType == nullptr || argumentType->getRuntime() != &runtime ||
            parameter.type->getRuntime() != &runtime || !parameter.type->acceptsArgument(argumentType)) {
            throw std::invalid_argument("Argument type mismatch for parameter '" + parameter.name + "'");
        }
        if (arguments[index].getStorage().size() != parameter.getStackValueCount()) {
            throw std::invalid_argument("Argument storage mismatch for parameter '" + parameter.name + "'");
        }
    }
}

struct StoredPropertySelection {
    const Type* type;
    NomadIndex offset;
};

StoredPropertySelection resolveStoredProperty(
    const Type* type,
    const std::span<const PropertyId> properties
) {
    if (properties.empty()) {
        throw std::invalid_argument("A stored property path cannot be empty");
    }
    auto offset = NomadIndex{0};
    for (const auto propertyId : properties) {
        if (type == nullptr || !type->isStruct()) {
            throw std::invalid_argument("Stored property paths require struct owners");
        }
        const auto* owner = type->asCompositeType();
        const auto* property = owner->getProperty(propertyId);
        if (property == nullptr || property->asNativeProperty() != nullptr) {
            throw std::invalid_argument("Stored property path does not match the struct type");
        }
        offset += owner->getFieldOffset(propertyId);
        type = property->getValueType();
    }
    return {type, offset};
}

std::vector<PropertyId> resolveStoredPropertyPath(const Type* type, const NomadStringView propertyPath) {
    std::vector<PropertyId> properties;
    auto start = NomadIndex{0};
    while (true) {
        const auto separator = propertyPath.find('.', start);
        const auto name = propertyPath.substr(start,
            separator == NomadStringView::npos ? separator : separator - start);
        if (name.empty()) {
            throw std::invalid_argument("A stored property path cannot contain empty names");
        }
        if (type == nullptr || !type->isStruct()) {
            throw std::invalid_argument("Stored property paths require struct owners");
        }
        const auto* property = type->asCompositeType()->getProperty(NomadString{name});
        if (property == nullptr || property->asNativeProperty() != nullptr) {
            throw std::invalid_argument("Unknown stored property '" + NomadString{name} +
                "' on type '" + type->getTypeName() + "'");
        }
        properties.push_back(property->getId());
        type = property->getValueType();
        if (separator == NomadStringView::npos) {
            return properties;
        }
        start = separator + 1;
    }
}

void retainDiagnostics(InterpreterContext& evaluation, std::span<const Diagnostic> diagnostics,
    const NomadString& sourceName) {
    for (auto diagnostic : diagnostics) {
        diagnostic.sourceName = sourceName;
        evaluation.addDiagnostic(diagnostic);
    }
}

void reportInputError(InterpreterContext& evaluation, const SourceText& source, const NomadString& message) {
    evaluation.addDiagnostic({DiagnosticSeverity::Error, message, source.sourceName,
        NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX});
}

class RegisterRestoration {
public:
    explicit RegisterRestoration(VirtualMachine& machine):
        m_machine(machine), m_result(machine.getResult()), m_intermediate(machine.getIntermediate()) {}

    ~RegisterRestoration() {
        m_machine.setResult(m_result);
        m_machine.setIntermediate(m_intermediate);
    }

private:
    VirtualMachine& m_machine;
    RuntimeValue m_result;
    RuntimeValue m_intermediate;
};

} // namespace

InterpreterValue Interpreter::callFunction(
    VirtualMachine& machine,
    const CallableId functionId,
    const std::span<const InterpreterValue> arguments
) const {
    const auto* function = machine.getRuntime()->getFunction(functionId);
    if (function == nullptr) {
        throw std::invalid_argument("Unknown function id");
    }
    const auto* native = function->asNativeFunction();
    if (native != nullptr && !native->isSignatureResolved()) {
        throw std::invalid_argument("Function '" + function->getName() + "' has an unresolved signature");
    }
    validateCallArguments(*machine.getRuntime(), *function, arguments);
    const auto* type = function->getReturnType();
    if (native != nullptr) {
        InterpreterCallFrame frame(machine, *function, arguments, type);
        machine.callNativeFunction(functionId);
        return InterpreterValue(type, frame.getResultStorage());
    }

    const auto* script = function->asScriptFunction();
    if (script == nullptr || script->getFunctionStart() == NOMAD_INVALID_INDEX ||
        script->getFunctionStart() >= script->getFunctionEnd()) {
        throw std::invalid_argument("Function '" + function->getName() + "' is not compiled");
    }
    if (script->getCaptureCount() != 0) {
        throw std::invalid_argument("Captured functions require closure arguments");
    }
    if (!type->isStruct() &&
        std::ranges::none_of(arguments, [](const auto& argument) { return argument.getType()->isStruct(); })) {
        std::vector<RuntimeValue> values;
        values.reserve(arguments.size());
        for (const auto& argument : arguments) {
            values.push_back(argument.getValue());
        }
        // Keep the scalar execution path's existing suspension behavior.
        const RegisterRestoration registers(machine);
        machine.setResult(RuntimeValue{});
        machine.run(script, values);
        try {
            InterpreterValue result(type, machine.getResult());
            type->freeValue(&machine.getResult());
            return result;
        } catch (...) {
            type->freeValue(&machine.getResult());
            throw;
        }
    }
    InterpreterCallFrame frame(machine, *function, arguments, type);
    machine.runPreparedFunction(script);
    return InterpreterValue(type, frame.getResultStorage());
}

InterpreterValue Interpreter::construct(
    VirtualMachine& machine,
    const CallableId constructorId,
    const std::span<const InterpreterValue> arguments
) const {
    const auto* constructor = machine.getRuntime()->getConstructor(constructorId);
    if (constructor == nullptr) {
        throw std::invalid_argument("Unknown constructor id");
    }
    validateCallArguments(*machine.getRuntime(), *constructor, arguments);
    const auto* type = constructor->getConstructedType();
    if (type == nullptr) {
        throw std::invalid_argument("Constructor has an unresolved constructed type");
    }
    if (const auto* script = constructor->asScriptConstructor()) {
        if (script->getFunctionStart() == NOMAD_INVALID_INDEX ||
            script->getFunctionStart() >= script->getFunctionEnd()) {
            throw std::invalid_argument("Constructor '" + constructor->getName() + "' is not compiled");
        }
    }
    InterpreterCallFrame frame(machine, *constructor, arguments, type);
    if (type->isStruct()) {
        if (constructor->asNativeConstructor() != nullptr) {
            machine.createNativeStruct(frame.getDestination(), constructorId);
        } else {
            machine.createScriptStruct(frame.getDestination(), constructorId);
        }
        return InterpreterValue(type, frame.getResultStorage());
    }
    auto* created = constructor->asNativeConstructor() != nullptr
        ? machine.createNativeObject(constructorId)
        : machine.createScriptObject(constructorId);
    const auto release = [](NomadObject* object) { object->releaseRef(); };
    const std::unique_ptr<NomadObject, decltype(release)> object(created, release);
    RuntimeValue value;
    value.setObjectValue(object.get());
    return InterpreterValue(type, value);
}

InterpreterValue Interpreter::getProperty(
    const InterpreterValue& receiver,
    const PropertyId propertyId
) const {
    return getStoredProperty(receiver, std::span<const PropertyId>{&propertyId, 1});
}

InterpreterValue Interpreter::getProperty(
    const InterpreterValue& receiver,
    const NomadStringView propertyPath
) const {
    const auto properties = resolveStoredPropertyPath(receiver.getType(), propertyPath);
    return getStoredProperty(receiver, properties);
}

InterpreterValue Interpreter::setProperty(
    const InterpreterValue& receiver,
    const PropertyId propertyId,
    const InterpreterValue& value
) const {
    return setStoredProperty(receiver, std::span<const PropertyId>{&propertyId, 1}, value);
}

InterpreterValue Interpreter::setProperty(
    const InterpreterValue& receiver,
    const NomadStringView propertyPath,
    const InterpreterValue& value
) const {
    const auto properties = resolveStoredPropertyPath(receiver.getType(), propertyPath);
    return setStoredProperty(receiver, properties, value);
}

InterpreterValue Interpreter::getStoredProperty(
    const InterpreterValue& receiver,
    const std::span<const PropertyId> properties
) const {
    const auto [type, offset] = resolveStoredProperty(receiver.getType(), properties);
    return InterpreterValue(type, type->getSize() == 0 ? std::span<const RuntimeValue>{} :
        receiver.getStorage().subspan(offset, type->getSize()));
}

InterpreterValue Interpreter::setStoredProperty(
    const InterpreterValue& receiver,
    const std::span<const PropertyId> properties,
    const InterpreterValue& value
) const {
    const auto [type, offset] = resolveStoredProperty(receiver.getType(), properties);
    if (type != value.getType()) {
        throw std::invalid_argument("Stored property assignment requires a matching value type");
    }
    const auto* receiverType = receiver.getType();
    std::vector<RuntimeValue> storage(receiver.getStorage().size());
    try {
        receiverType->copyValue(receiver.getStorage().data(), storage.data());
        std::vector<RuntimeValue> scratch(type->getAssignmentScratchSize());
        type->assignValue(value.getStorage().data(),
            type->getSize() == 0 ? nullptr : storage.data() + offset,
            scratch.empty() ? nullptr : scratch.data());
        InterpreterValue result(receiverType, storage);
        receiverType->freeValue(storage.data());
        return result;
    } catch (...) {
        receiverType->freeValue(storage.data());
        throw;
    }
}

bool Interpreter::evaluateExpression(const NomadString& source, InterpreterContext& evaluation) const {
    return evaluateExpression(SourceText{evaluation.m_function->getPath(), source}, evaluation);
}

bool Interpreter::evaluateExpression(
    const SourceText& source,
    InterpreterContext& evaluation,
    NomadIndex sourceLine
) const {
    evaluation.clearError();
    evaluation.clearResult();
    evaluation.setLoopControl(LoopControl::None);
    evaluation.validateFrame();
    auto compiler = evaluation.m_runtime->createCompiler();
    CompilerContext context(compiler.get(), CompilerMode::Interpreter, &evaluation);
    auto* function = evaluation.m_function.get();
    if (sourceLine == 0 || sourceLine == NOMAD_INVALID_INDEX) {
        sourceLine = 1;
    }
    Tokenizer tokens(&context, source.sourceName, NomadString(sourceLine - 1, '\n') + source.text);
    retainDiagnostics(evaluation, context.getDiagnostics(), source.sourceName);
    if (context.hasError()) {
        return false;
    }
    if (tokens.getLineCount() != sourceLine || !tokens.nextLine()) {
        reportInputError(evaluation, source, "Interpreter requires one non-empty expression");
        return false;
    }
    auto expression = parser::parseExpression(&context, function, &tokens);
    if (expression != nullptr && parser::expectEndOfLine(&context, function, &tokens)) {
        (void)expression->resolve(&context, function);
    }
    // Tokenization diagnostics were already retained above.
    const auto retainedDiagnostics = evaluation.getDiagnostics().size();
    retainDiagnostics(evaluation, context.getDiagnostics().subspan(retainedDiagnostics), source.sourceName);
    if (context.hasError()) {
        return false;
    }
    if (expression == nullptr || expression->getType() == nullptr || expression->getType()->isVoid()) {
        reportInputError(evaluation, source, "Interpreter expression must return a value");
        return false;
    }
    auto* machine = evaluation.getVirtualMachine();
    const RegisterRestoration registers(*machine);
    try {
        expression->evaluate(evaluation);
    } catch (...) {
        evaluation.clearResult();
        throw;
    }
    return !evaluation.hasError();
}

NomadBoolean Interpreter::execute(const NomadString& source, InterpreterContext& evaluation) const {
    return execute(SourceText{evaluation.m_function->getPath(), source}, evaluation);
}

NomadBoolean Interpreter::execute(const SourceText& source, InterpreterContext& evaluation) const {
    evaluation.validateFrame();
    evaluation.clearError();
    evaluation.clearResult();
    evaluation.setLoopControl(LoopControl::None);
    const RegisterRestoration registers(*evaluation.getVirtualMachine());
    try {
        auto compiler = evaluation.m_runtime->createCompiler();
        CompilerContext compilerContext(compiler.get(), CompilerMode::Interpreter, &evaluation);
        Tokenizer tokenizer(&compilerContext, source.sourceName, source.text);
        if (compilerContext.hasError()) {
            retainDiagnostics(evaluation, compilerContext.getDiagnostics(), source.sourceName);
            return false;
        }
        if (!tokenizer.nextLine()) {
            evaluation.setVoidResult();
            return true;
        }

        const auto& firstToken = tokenizer.currentToken();
        const auto isAssignment = parser::hasAssignmentOperator(&tokenizer);
        const auto isStatement = firstToken.type == TokenType::Identifier &&
                                 compiler->isStatement(firstToken.textValue);
        const auto functionId = firstToken.type == TokenType::Identifier
            ? evaluation.m_runtime->getCallableId(firstToken.textValue)
            : CallableId{};
        const auto* function = evaluation.m_runtime->getFunction(functionId);
        const auto isValueReturningCall =
            function != nullptr && function->getReturnType() != nullptr && !function->getReturnType()->isVoid();
        const auto line = tokenizer.getLineIndex();
        const auto column = tokenizer.getColumnIndex();
        const auto parseAsExpression = tokenizer.getLineCount() == 1 && !isAssignment && !isStatement &&
                                       (isValueReturningCall || function == nullptr);

        std::unique_ptr<Statement> statement;
        StatementList statements;
        if (parseAsExpression) {
            auto expression = parser::parseExpression(&compilerContext, evaluation.m_function.get(), &tokenizer);
            if (expression != nullptr &&
                parser::expectEndOfLine(&compilerContext, evaluation.m_function.get(), &tokenizer)) {
                if (auto* member = expression->asNativeExpression()) {
                    member->setAllowVoid(true);
                }
                statement = std::make_unique<ExpressionStatement>(
                    line,
                    column,
                    std::move(expression)
                );
            }
        } else {
            do {
                statements.addStatement(parser::parseLine(&compilerContext, evaluation.m_function.get(), &tokenizer));
            } while (tokenizer.nextLine());
        }
        statements.addStatement(std::move(statement));

        if (compilerContext.hasError()) {
            retainDiagnostics(evaluation, compilerContext.getDiagnostics(), source.sourceName);
            return false;
        }

        if (statements.isEmpty()) {
            evaluation.setVoidResult();
            return true;
        }

        if (!statements.resolve(&compilerContext, evaluation.m_function.get())) {
            retainDiagnostics(evaluation, compilerContext.getDiagnostics(), source.sourceName);
            if (!evaluation.hasError()) {
                reportInputError(evaluation, source, "Failed to resolve interpreter input");
            }
            return false;
        }

        retainDiagnostics(evaluation, compilerContext.getDiagnostics(), source.sourceName);
        statements.evaluate(evaluation);
        return !evaluation.hasError();
    } catch (const NomadException& exception) {
        evaluation.clearResult();
        reportInputError(evaluation, source, exception.what());
        return false;
    } catch (const std::exception& exception) {
        evaluation.clearResult();
        reportInputError(evaluation, source, exception.what());
        return false;
    }
}

} // namespace nomad
