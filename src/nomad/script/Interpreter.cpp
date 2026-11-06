// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Interpreter.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Expression.hpp>
#include <nomad/compiler/FormatStringCompiler.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/SyntaxTree.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/log/Logger.hpp>

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

const Type* validateMethodReceiver(
    const Runtime& runtime,
    const Method& method,
    const InterpreterValue& receiver
) {
    const auto* receiverType = receiver.getType();
    const auto* ownerType = method.getClassType();
    if (receiverType == nullptr || ownerType == nullptr || receiverType->getRuntime() != &runtime ||
        ownerType->getRuntime() != &runtime || !ownerType->acceptsArgument(receiverType)) {
        throw std::invalid_argument("Method receiver type does not match its owner");
    }
    return receiverType;
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

Interpreter::Interpreter(VirtualMachine& machine):
    m_virtualMachine(machine)
{
}

VirtualMachine& Interpreter::getVirtualMachine() const {
    return m_virtualMachine;
}

InterpreterValue Interpreter::runFunction(
    const CallableId functionId,
    const std::span<const InterpreterValue> arguments
) const {
    auto& machine = m_virtualMachine;
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
        runNativeCallable(native->getFn());
        return frame.getResult();
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
    if (!runScriptCallable(*script)) {
        machine.fault("Aggregate script calls cannot yield");
    }
    return frame.getResult();
}

bool Interpreter::runScriptCallable(
    const ScriptCallableBase& callable,
    const RuntimeValue& receiver
) const {
    const auto state = m_virtualMachine.captureExecutionState();
    try {
        m_virtualMachine.enterScriptFrame(callable.getFunctionStart(), receiver);
        m_virtualMachine.executeScript();
    } catch (...) {
        m_virtualMachine.restoreExecutionState(state);
        throw;
    }
    const auto completed = !m_virtualMachine.hasYielded();
    m_virtualMachine.restoreExecutionState(state);
    return completed;
}

void Interpreter::runNativeCallable(
    const NativeFunctionFn& callback,
    const RuntimeValue& receiver
) const {
    auto state = m_virtualMachine.captureExecutionState();
    const auto restore = [&]() {
        // Scalar native calls retain their existing yield/stop behavior.
        state.running = m_virtualMachine.isRunning();
        state.yielded = m_virtualMachine.hasYielded();
        m_virtualMachine.restoreExecutionState(state);
    };
    m_virtualMachine.enterNativeFrame(receiver);
    try {
        callback(&m_virtualMachine);
    } catch (...) {
        restore();
        throw;
    }
    restore();
}

void Interpreter::runNativeStructMethod(const NativeMethod& method) const {
    const auto state = m_virtualMachine.captureExecutionState();
    auto invocation = state;
    invocation.yielded = false;
    m_virtualMachine.restoreExecutionState(invocation);
    try {
        runNativeCallable(method.getFn());
        if (m_virtualMachine.hasYielded()) {
            m_virtualMachine.fault("Struct method calls cannot yield");
        }
    } catch (...) {
        m_virtualMachine.restoreExecutionState(state);
        throw;
    }
    m_virtualMachine.restoreExecutionState(state);
}

InterpreterValue Interpreter::runObjectMethod(
    const InterpreterValue& receiver,
    const CallableId methodId,
    const std::span<const InterpreterValue> arguments
) const {
    auto& machine = m_virtualMachine;
    const auto* runtime = machine.getRuntime();
    const auto* method = runtime->getMethod(methodId);
    if (method == nullptr) {
        throw std::invalid_argument("Unknown method id");
    }
    validateCallArguments(*runtime, *method, arguments);
    const auto* receiverType = validateMethodReceiver(*runtime, *method, receiver);
    const auto* type = method->getReturnType();
    if (receiverType->isStruct() || type->isStruct() ||
        std::ranges::any_of(arguments, [](const auto& argument) { return argument.getType()->isStruct(); })) {
        throw std::invalid_argument("Interpreter method calls do not yet support struct storage");
    }
    if (receiverType->asClassType() != nullptr && receiver.getValue().getObjectValue() == nullptr) {
        throw std::invalid_argument("Method receiver cannot be null");
    }
    const auto* script = method->asScriptMethod();
    if (script != nullptr && (script->getFunctionStart() == NOMAD_INVALID_INDEX ||
        script->getFunctionStart() >= script->getFunctionEnd())) {
        throw std::invalid_argument("Method '" + method->getName() + "' is not compiled");
    }
    if (script == nullptr && method->asNativeMethod() == nullptr) {
        throw std::invalid_argument("Unsupported method implementation");
    }
    InterpreterCallFrame frame(machine, *method, arguments, type, &receiver);
    if (script != nullptr) {
        if (!runScriptCallable(*script, receiver.getValue())) {
            machine.fault("Script methods cannot yield");
        }
    } else {
        runNativeCallable(method->asNativeMethod()->getFn(), receiver.getValue());
    }
    return frame.getResult();
}

StructMethodResult Interpreter::runStructMethod(
    const InterpreterValue& receiver,
    const CallableId methodId,
    const std::span<const InterpreterValue> arguments
) const {
    auto& machine = m_virtualMachine;
    const auto* runtime = machine.getRuntime();
    const auto* method = runtime->getMethod(methodId);
    if (method == nullptr) {
        throw std::invalid_argument("Unknown method id");
    }
    validateCallArguments(*runtime, *method, arguments);
    const auto* receiverType = validateMethodReceiver(*runtime, *method, receiver);
    if (!receiverType->isStruct()) {
        throw std::invalid_argument("Struct method calls require a struct receiver");
    }
    const auto* script = method->asScriptMethod();
    if (script != nullptr && (script->getFunctionStart() == NOMAD_INVALID_INDEX ||
        script->getFunctionStart() >= script->getFunctionEnd())) {
        throw std::invalid_argument("Method '" + method->getName() + "' is not compiled");
    }
    if (script == nullptr && method->asNativeMethod() == nullptr) {
        throw std::invalid_argument("Unsupported method implementation");
    }
    InterpreterCallFrame frame(machine, *method, arguments, method->getReturnType(), &receiver);
    if (script != nullptr) {
        if (!runScriptCallable(*script)) {
            machine.fault("Struct method calls cannot yield");
        }
    } else {
        runNativeStructMethod(*method->asNativeMethod());
    }
    return {frame.getReceiver(), frame.getResult()};
}

InterpreterValue Interpreter::construct(
    const CallableId constructorId,
    const std::span<const InterpreterValue> arguments
) const {
    auto& machine = m_virtualMachine;
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
        return frame.getResult();
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

std::optional<InterpreterValue> Interpreter::evaluateFormatExpression(
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
        return std::nullopt;
    }
    if (tokens.getLineCount() != sourceLine || !tokens.nextLine()) {
        reportInputError(evaluation, source, "Interpreter requires one non-empty expression");
        return std::nullopt;
    }
    auto expression = parser::parseExpression(&context, function, &tokens);
    if (expression != nullptr && parser::expectEndOfLine(&context, function, &tokens)) {
        (void)expression->resolve(&context, function);
    }
    // Tokenization diagnostics were already retained above.
    const auto retainedDiagnostics = evaluation.getDiagnostics().size();
    retainDiagnostics(evaluation, context.getDiagnostics().subspan(retainedDiagnostics), source.sourceName);
    if (context.hasError()) {
        return std::nullopt;
    }
    if (expression == nullptr || expression->getType() == nullptr || expression->getType()->isVoid()) {
        reportInputError(evaluation, source, "Interpreter expression must return a value");
        return std::nullopt;
    }
    auto* machine = evaluation.getVirtualMachine();
    const RegisterRestoration registers(*machine);
    try {
        return expression->evaluate(evaluation);
    } catch (...) {
        evaluation.clearResult();
        throw;
    }
}

DynamicFormatResult Interpreter::formatString(
    const NomadStringView format,
    InterpreterContext& context,
    const NomadStringView sourceName,
    const NomadIndex line,
    const NomadIndex column
) const {
    DynamicFormatResult result;
    const auto parsed = parseFormatTemplate(format);
    const auto appendError = [&](const FormatTemplateSegment& segment, const NomadString& message) {
        result.text += "{" + message + "}";
        result.diagnostics.push_back({message, NomadString{sourceName}, line, column,
            segment.offset, segment.length});
        log::error("Dynamic format error: source=" + NomadString{sourceName} +
            ", line=" + std::to_string(line) + ", column=" + std::to_string(column) +
            ", offset=" + std::to_string(segment.offset) + ", length=" + std::to_string(segment.length) +
            ", message=" + message);
    };
    for (const auto& segment : parsed.segments) {
        if (segment.type == FormatTemplateSegmentType::Literal) {
            result.text += segment.text;
        } else if (segment.type == FormatTemplateSegmentType::Error) {
            const auto& diagnostic = parsed.diagnostics.at(*segment.diagnosticIndex);
            appendError(segment, diagnostic.message == "Missing '}' in format string"
                ? "missing `}`" : "empty expression");
        } else if (const auto value = evaluateFormatExpression(
            SourceText{NomadString{sourceName}, segment.text}, context, line
        )) {
            result.text += value->getType()->toString(value->getValue());
        } else {
            const auto diagnostics = context.getDiagnostics();
            const auto error = std::ranges::find_if(diagnostics, [](const Diagnostic& diagnostic) {
                return diagnostic.severity == DiagnosticSeverity::Error;
            });
            auto message = error != diagnostics.end() ? error->message : *context.getError();
            constexpr NomadStringView unknownIdentifier = "Unknown identifier: ";
            if (message.starts_with(unknownIdentifier)) {
                message = "invalid identifier: '" + message.substr(unknownIdentifier.size()) + "'";
            }
            appendError(segment, message);
        }
        context.clearError();
        context.clearResult();
    }
    return result;
}

NomadBoolean Interpreter::execute(const NomadString& source, InterpreterContext& evaluation) const {
    return execute(SourceText{evaluation.m_function->getPath(), source}, evaluation);
}

NomadBoolean Interpreter::execute(const SourceText& source, InterpreterContext& evaluation) const {
    evaluation.clearError();
    evaluation.clearResult();
    evaluation.setLoopControl(LoopControl::None);
    const RegisterRestoration registers(*evaluation.getVirtualMachine());
    try {
        evaluation.validateFrame();
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
        evaluation.setResult(statements.evaluate(evaluation));
        return !evaluation.hasError();
    } catch (const NomadException& exception) {
        evaluation.clearResult();
        if (!evaluation.hasError()) {
            reportInputError(evaluation, source, exception.what());
        }
        return false;
    } catch (const std::exception& exception) {
        evaluation.clearResult();
        if (!evaluation.hasError()) {
            reportInputError(evaluation, source, exception.what());
        }
        return false;
    }
}

} // namespace nomad
