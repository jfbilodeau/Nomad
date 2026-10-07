// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Tokenizer.hpp>
#include <nomad/compiler/StatementParsers.hpp>

#include <nomad/compiler/statements/EventStatement.hpp>
#include <nomad/compiler/statements/ReturnStatement.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/String.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/CompilerException.hpp>

#include <filesystem>
#include <fstream>

namespace nomad {

// Utility to concatenate directory / filename
NomadString concatPath(const NomadString& directory, const NomadString& filename) {
    if (filename.empty() && directory.empty()) {
        // I feel like this should be an error...
        return "";
    }

    if (filename.empty()) {
        return directory;
    }

    if (directory.empty()) {
        return filename;
    }

    auto lastChar = directory.back();

    if (lastChar == '/' || lastChar == '\\') {
        return directory + filename;
    }

    return directory + '/' + filename;
}

Compiler::Compiler(Runtime *runtime, std::vector<Instruction>& instructions):
    m_runtime(runtime),
    m_instructions(instructions)
{
    if (m_instructions.empty()) {
        // Add op_stop instruction so that the return of a function is a stop instruction
        addOpCode(OpCodes::op_stop);
    }

    registerParseStatementFn("const", parseConstStatement, preParseConstStatement);
    registerParseStatementFn("event", parseEventStatement, preParseEventStatement);
    registerParseStatementFn("fun", parseFunStatement, preParseFunStatement);
    registerParseStatementFn("assert", parseAssertStatement);
    registerParseStatementFn("if", parseIfStatement);
    registerParseStatementFn("params", nullptr, preParseParamsStatement);
    registerParseStatementFn("return", parseReturnStatement);

}

Compiler::~Compiler() = default;

Runtime* Compiler::getRuntime() const {
    return m_runtime;
}

void Compiler::reportError(const NomadString& message) const {
    throw CompilerException(message);
}

void Compiler::reportError(const NomadString& message, const Function* function, const Tokenizer* tokenizer) const {
    reportError(message, function->getName(), tokenizer->getLineIndex(), tokenizer->getColumnIndex());
}

void Compiler::reportError(const NomadString& message, const NomadString& functionName, const NomadIndex line, const NomadIndex column) const {
    const auto errorMessage = message + ": '" + functionName + "' at line " + toString(line) + ", column " + toString(column);

    reportError(errorMessage);
}

void Compiler::reportInternalError(const NomadString& message) const {
    throw NomadBug("Internal error: " + message);
}

void Compiler::registerParseStatementFn(
    const NomadString& name,
    ParseStatementFn fn,
    PreParseStatementFn preFn
) {
    m_statements.emplace(name, ParseStatementFnRegistration{ preFn, fn });
}

bool Compiler::isStatement(const NomadString& name) const {
    return m_statements.find(name) != m_statements.end();
}

bool Compiler::getParseStatementFn(const NomadString& name, ParseStatementFn& fn) const {
    const auto it = m_statements.find(name);

    if (it != m_statements.end()) {
        fn = it->second.fn;

        return true;
    }

    return false;
}

bool Compiler::getGetPreParseStatementFn(const NomadString& name, PreParseStatementFn& fn) const {
    const auto it = m_statements.find(name);

    if (it != m_statements.end()) {
        fn = it->second.preFn;

        return true;
    }

    return false;
}

void Compiler::getRegisteredStatements(std::vector<NomadString>& parsers) const {
    for (auto& statement: m_statements) {
        parsers.push_back(statement.first);
    }
}

void Compiler::registerUnaryOperator(
    UnaryOperator op,
    const Type* operand,
    const Type* result,
    const NomadString& opCodeName,
    UnaryFoldingFn fn
) {
    m_runtime->registerUnaryOperator(op, operand, result, opCodeName, fn);
}

void Compiler::registerBinaryOperator(
    BinaryOperator op,
    const Type* lhs,
    const Type* rhs,
    const Type* result,
    const NomadString& opCodeName,
    BinaryFoldingFn fn
) {
    m_runtime->registerBinaryOperator(op, lhs, rhs, result, opCodeName, fn);
}

const Type* Compiler::getUnaryOperatorResultType(UnaryOperator op, const Type* operandType) const {
    return m_runtime->getUnaryOperatorResultType(op, operandType);
}

const Type* Compiler::getBinaryOperatorResultType(BinaryOperator op, const Type* lhsType, const Type* rhsType) const {
    return m_runtime->getBinaryOperatorResultType(op, lhsType, rhsType);
}

NomadId Compiler::getUnaryOperatorOpCodeId(UnaryOperator op, const Type* operand) const {
    return m_runtime->getUnaryOperatorOpCodeId(op, operand);
}

NomadId Compiler::getBinaryOperatorOpCodeId(const BinaryOperator op, const Type* lhs, const Type* rhs) const {
    return m_runtime->getBinaryOperatorOpCodeId(op, lhs, rhs);
}

bool Compiler::foldUnary(
    UnaryOperator op,
    const Type* operandType,
    const RuntimeValue& value,
    RuntimeValue& result
) const {
    return m_runtime->foldUnary(op, operandType, value, result);
}

bool Compiler::foldBinary(
    BinaryOperator op,
    const Type* lhsType,
    const RuntimeValue& lhs,
    const Type* rhsType,
    const RuntimeValue& rhs,
    RuntimeValue& result
) const {
    return m_runtime->foldBinary(op, lhsType, lhs, rhsType, rhs, result);
}

IdentifierType Compiler::getIdentifierType(const NomadString& name, const Function* function) const {
    if (m_runtime->getKeywordId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::Keyword;
    }

    if (isStatement(name)) {
        return IdentifierType::Statement;
    }

    const auto callable = m_runtime->getCallableId(name);

    if (callable.isValid() && callable.kind == CallableKind::NativeFunction) {
        return IdentifierType::NativeFunction;
    }

    if (m_runtime->getConstantId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::Constant;
    }

    if (callable.isValid() && callable.kind == CallableKind::Function) {
        return IdentifierType::Function;
    }

    if (m_runtime->getDynamicVariableId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::DynamicVariable;
    }

    if (m_runtime->getVariableContextIdByPrefix(name) != NOMAD_INVALID_ID) {
        return IdentifierType::ContextVariable;
    }

    if (function == nullptr) {
        return IdentifierType::Unknown;
    }

    if (function->getParameterId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::Parameter;
    }

    if (function->getVariableId(name) != NOMAD_INVALID_ID) {
        return IdentifierType::FunctionVariable;
    }

    return IdentifierType::Unknown;
}

void Compiler::getIdentifierDefinition(const NomadString& name, const Function* function, IdentifierDefinition& definition) const {
    if (name.empty()) {
        definition.identifierType = IdentifierType::Unknown;

        return;
    }

    const auto firstCharacter = static_cast<unsigned char>(name[0]);
    if (!(std::isalpha(firstCharacter) || firstCharacter == '_')) {
        definition.identifierType = IdentifierType::Unknown;

        return;
    }

    definition.identifierType = getIdentifierType(name, function);

    if (definition.identifierType == IdentifierType::Keyword || definition.identifierType == IdentifierType::Statement) {
        return;
    }

    const auto callable = m_runtime->getCallableId(name);

    if (callable.isValid()) {
        if (callable.kind == CallableKind::NativeFunction) {
            definition.identifierType = IdentifierType::NativeFunction;
            definition.nativeFunctionId = callable.id;
        } else {
            definition.identifierType = IdentifierType::Function;
            definition.functionId = callable.id;
        }

        definition.valueType = m_runtime->getCallableReturnType(callable);

        return;
    }

    const NomadId eventId = m_runtime->getEventId(name);

    if (eventId != NOMAD_INVALID_ID) {
        definition.identifierType = IdentifierType::Event;
        definition.eventId = eventId;

        return;
    }

    const NomadId constantId = m_runtime->getConstantId(name);

    if (constantId != NOMAD_INVALID_ID) {
        definition.identifierType = IdentifierType::Constant;
        definition.variableId = constantId;
        definition.valueType = m_runtime->getConstantType(constantId);

        return;
    }

    const NomadId dynamicVariableId = m_runtime->getDynamicVariableId(name);

    if (dynamicVariableId != NOMAD_INVALID_ID) {
        definition.identifierType = IdentifierType::DynamicVariable;
        definition.variableId = dynamicVariableId;
        definition.valueType = m_runtime->getDynamicVariableType(dynamicVariableId);

        return;
    }

    const NomadId contextId = m_runtime->getVariableContextIdByPrefix(name);

    if (contextId != NOMAD_INVALID_ID) {
        const auto variableId = m_runtime->getContextVariableId(name);

        if (variableId == NOMAD_INVALID_ID) {
            definition.identifierType = IdentifierType::Unknown;

            return;
        }

        definition.variableId = variableId;
        definition.valueType = m_runtime->getContextVariableType(variableId);

        return;
    }

    if (function == nullptr) {
        definition.identifierType = IdentifierType::Unknown;

        return;
    }

    // Check if this is a local parameter
    const NomadId parameterId = function->getParameterId(name);

    if (parameterId != NOMAD_INVALID_ID) {
        definition.identifierType = IdentifierType::Parameter;
        definition.variableId = parameterId;
        definition.valueType = function->getParameterType(parameterId);

        return;
    }

    // Check if this is a local variable
    const NomadId variableId = function->getVariableId(name);

    if (variableId != NOMAD_INVALID_ID) {
        definition.identifierType = IdentifierType::FunctionVariable;
        definition.variableId = variableId;
        definition.valueType = function->getVariableType(variableId);

        return;
    }

    definition.identifierType = IdentifierType::Unknown;
}


NomadIndex Compiler::getOpCodeSize() const {
    return m_instructions.size();
}

NomadIndex Compiler::addOpCode(NomadId opCode) {
    auto fn = m_runtime->getInstructionFn(opCode);

    if (fn == nullptr) {
        reportInternalError("Unknown op code: " + toString(opCode));
    }

    m_instructions.emplace_back(fn);

    return m_instructions.size() - 1;
}

NomadIndex Compiler::addOpCode(const NomadString& opCodeName) {
    const auto opCodeId = m_runtime->getInstructionId(opCodeName);

    if (opCodeId == NOMAD_INVALID_ID) {
        reportError("Unknown instruction: " + opCodeName);
    }

    return addOpCode(opCodeId);
}

NomadIndex Compiler::addOpCode(const InstructionFn opCode) {
    if (opCode == nullptr || m_runtime->getInstructionId(opCode) == NOMAD_INVALID_ID) {
        reportInternalError("BUG: Unregistered op code function");
    }

    m_instructions.emplace_back(opCode);

    return m_instructions.size() - 1;
}

NomadIndex Compiler::addId(const NomadId id) {
    const auto index = m_instructions.size();

    const RuntimeValue functionValue(id);

    m_instructions.emplace_back(functionValue);

    return index;
}

NomadIndex Compiler::addIndex(const NomadIndex index) {
    const auto instruction = m_instructions.size();

    m_instructions.emplace_back(RuntimeValue(index));

    return instruction;
}

NomadIndex Compiler::addInteger(NomadInteger value) {
    const auto instruction = m_instructions.size();

    m_instructions.emplace_back(RuntimeValue(value));

    return instruction;
}

NomadIndex Compiler::addFloat(NomadFloat value) {
    const auto instruction = m_instructions.size();

    m_instructions.emplace_back(RuntimeValue(value));

    return instruction;
}

NomadIndex Compiler::addLoadValue(const Type* type, const RuntimeValue& value, ExpressionTarget target) {
    if (type == getRuntime()->getBooleanType()) {
        return addLoadBooleanValue(value.getBooleanValue(), target);
    } else if (type == getRuntime()->getIntegerType()) {
        return addLoadIntegerValue(value.getIntegerValue(), target);
    } else if (type == getRuntime()->getFloatType()) {
        return addLoadFloatValue(value.getFloatValue(), target);
    } else if (type->isString()) {
        return addLoadStringValue(value.getStringValue(), target);
    }

    reportInternalError("Unknown type: " + type->getTypeName());
}

NomadIndex Compiler::addLoadBooleanValue(const bool value, const ExpressionTarget target) {
    switch (target) {
    case ExpressionTarget::Result:
        if (value) {
            return addOpCode(OpCodes::op_boolean_load_true_r);
        } else {
            return addOpCode(OpCodes::op_boolean_load_false_r);
        }
    case ExpressionTarget::Intermediate:
        if (value) {
            return addOpCode(OpCodes::op_boolean_load_true_i);
        } else {
            return addOpCode(OpCodes::op_boolean_load_false_i);
        }
    case ExpressionTarget::Stack:
        if (value) {
            return addOpCode(OpCodes::op_boolean_push_true);
        } else {
            return addOpCode(OpCodes::op_boolean_push_false);
        }
    }

    this->reportInternalError("BUG: Invalid expression target for boolean value");
}

NomadIndex Compiler::addLoadIntegerValue(const NomadInteger value, const ExpressionTarget target) {
    switch (target) {
    case ExpressionTarget::Result:
        if (value == 0) {
            return addOpCode(OpCodes::op_integer_load_zero_r);
        }

        if (value == 1) {
            return addOpCode(OpCodes::op_integer_load_one_r);
        }

        addOpCode(OpCodes::op_integer_load_r);
        return addInteger(value);

    case ExpressionTarget::Intermediate:
        if (value == 0) {
            return addOpCode(OpCodes::op_integer_load_zero_i);
        }
        if (value == 1) {
            return addOpCode(OpCodes::op_integer_load_one_i);
        }

        addOpCode(OpCodes::op_integer_load_i);
        return addInteger(value);

    case ExpressionTarget::Stack:
        if (value == 0) {
            return addOpCode(OpCodes::op_integer_push_zero);
        }

        if (value == 1) {
            return addOpCode(OpCodes::op_integer_push_one);
        }

        addOpCode(OpCodes::op_integer_push_n);
        return addInteger(value);
    }

    this->reportInternalError("BUG: Invalid expression target for float value");
}

NomadIndex Compiler::addLoadFloatValue(NomadFloat value, const ExpressionTarget target) {
    switch (target) {
    case ExpressionTarget::Result:
        if (value == 0) {
            return addOpCode(OpCodes::op_float_load_zero_r);
        }
        if (value == 1) {
            return addOpCode(OpCodes::op_float_load_one_r);
        }

        addOpCode(OpCodes::op_float_load_r);
        return addFloat(value);

    case ExpressionTarget::Intermediate:
        if (value == 0) {
            return addOpCode(OpCodes::op_float_load_zero_i);
        }
        if (value == 1) {
            return addOpCode(OpCodes::op_float_load_one_i);
        }

        addOpCode(OpCodes::op_float_load_i);
        return addFloat(value);

    case ExpressionTarget::Stack:
        if (value == 0) {
            return addOpCode(OpCodes::op_float_push_zero);
        }
        if (value == 1) {
            return addOpCode(OpCodes::op_float_push_one);
        }
        addOpCode(OpCodes::op_float_push_n);
        return addFloat(value);
    }

    this->reportInternalError("BUG: Invalid expression target for float value");
}

NomadIndex Compiler::addLoadStringValue(const NomadString& value, const ExpressionTarget target) {
    const auto stringId = getRuntime()->registerString(value);

    switch (target) {
    case ExpressionTarget::Result:
        addOpCode(OpCodes::op_string_load_r);
        return addId(stringId);

    case ExpressionTarget::Intermediate:
        addOpCode(OpCodes::op_string_load_i);
        return addId(stringId);

    case ExpressionTarget::Stack:
        addOpCode(OpCodes::op_string_push_n);
        return addId(stringId);
    }

    this->reportInternalError("BUG: Invalid expression target for string value");
}

NomadIndex Compiler::addLoadStringReference(const NomadString& value, const ExpressionTarget target) {
    const auto stringId = getRuntime()->registerString(value);

    switch (target) {
    case ExpressionTarget::Result:
        addOpCode(OpCodes::op_stringref_load_r);
        return addId(stringId);

    case ExpressionTarget::Intermediate:
        addOpCode(OpCodes::op_stringref_load_i);
        return addId(stringId);

    case ExpressionTarget::Stack:
        addOpCode(OpCodes::op_stringref_push);
        return addId(stringId);
    }

    this->reportInternalError("BUG: Invalid expression target for string reference");
}

NomadIndex Compiler::addLoadTarget(const TargetOpCodes& opCodes, const NomadId operand, const ExpressionTarget target) {
    switch (target) {
    case ExpressionTarget::Result:
        addOpCode(opCodes.result);
        return addId(operand);

    case ExpressionTarget::Intermediate:
        addOpCode(opCodes.intermediate);
        return addId(operand);

    case ExpressionTarget::Stack:
        addOpCode(opCodes.stack);
        return addId(operand);
    }

    reportInternalError("BUG: Invalid expression target");
}

NomadIndex Compiler::addLoadFunctionVariable(const NomadId variableId, const Type* type, const ExpressionTarget target) {
    return addLoadTarget(type->getTypeOpCodes()->loadFunctionVariable, variableId, target);
}

NomadIndex Compiler::addLoadDynamicVariable(const NomadId variableId, const Type* type, const ExpressionTarget target) {
    return addLoadTarget(type->getTypeOpCodes()->loadDynamicVariable, variableId, target);
}

NomadIndex Compiler::addLoadContextVariable(const NomadId contextVariableId, const Type* type, const ExpressionTarget target) {
    return addLoadTarget(type->getTypeOpCodes()->loadContextVariable, contextVariableId, target);
}

NomadIndex Compiler::addLoadParameter(const NomadIndex parameterId, const Type* type, const ExpressionTarget target) {
    return addLoadTarget(type->getTypeOpCodes()->loadParameter, toNomadId(parameterId), target);
}

NomadIndex Compiler::addPushResult(const Type* type) {
    return addOpCode(type->getTypeOpCodes()->copyResultToStack);
}

void Compiler::addMoveResult(const Type* type, const ExpressionTarget target) {
    switch (target) {
    case ExpressionTarget::Result:
        break;

    case ExpressionTarget::Intermediate:
        addOpCode(type->getTypeOpCodes()->moveResultToIntermediate);
        break;

    case ExpressionTarget::Stack:
        addOpCode(type->getTypeOpCodes()->moveResultToStack);
        break;
    }
}

NomadIndex Compiler::addPushIntermediate(const Type* type) {
    return addOpCode(type->getTypeOpCodes()->pushIntermediate);
}

NomadIndex Compiler::addPopIntermediate(const Type* type) {
    return addOpCode(type->getTypeOpCodes()->popIntermediate);
}

void Compiler::addFreeValue(const InstructionFn freeOpCode) {
    if (freeOpCode != nullptr) {
        addOpCode(freeOpCode);
    }
}

void Compiler::addFreeValue(const InstructionFn freeOpCode, const NomadIndex operand) {
    if (freeOpCode != nullptr) {
        addOpCode(freeOpCode);
        addIndex(operand);
    }
}

NomadIndex Compiler::addFunctionCall(const NomadId targetFunctionId, Function* function, const ArgumentList* arguments) {
    const auto targetFunction = getRuntime()->getFunction(targetFunctionId);

    if (targetFunction == nullptr) {
        reportInternalError("Unknown function id: " + toString(targetFunctionId));
    }

    const auto argumentCount = arguments->getArgumentCount();

    if (argumentCount != targetFunction->getParameterCount()) {
        reportInternalError("BUG: Argument count does not match parameters of function '" + targetFunction->getName() + "'");
    }

    arguments->compile(this, function);

    addOpCode(OpCodes::op_call_function);

    if (targetFunction->getFunctionStart() == NOMAD_INVALID_INDEX) {
        addFunctionLink(targetFunctionId, m_instructions.size());

        addIndex(NOMAD_INVALID_INDEX); // Placeholder for function jump index
    } else {
        addIndex(targetFunction->getFunctionStart());
    }

    if (argumentCount != 0) {
        addFreeArguments(arguments);

        addOpCode(OpCodes::op_pop_n);
        addIndex(argumentCount);
    }

    return m_instructions.size() - 1;
}

void Compiler::addFreeArguments(const ArgumentList* arguments) {
    // Arguments are pushed right-to-left, so the first argument is at the top of the stack. Offsets account for
    // arguments occupying more than one slot.
    NomadIndex stackOffset = 0;

    for (const auto& argument : arguments->getArguments()) {
        if (const auto* pushedType = argument->getPushedType()) {
            addFreeValue(pushedType->getTypeOpCodes()->freeStack, stackOffset);
        }

        stackOffset += argument->getStackValueCount();
    }
}

NomadIndex Compiler::addNativeFunctionCall(const NomadId nativeFunctionId, Function* function, const ArgumentList* arguments) {
    arguments->compile(this, function);

    NativeFunctionDefinition nativeFunction;

    const auto result = m_runtime->getNativeFunctionDefinition(nativeFunctionId, nativeFunction);

    if (result == false) {
        reportInternalError("Unknown native function: " + toString(nativeFunctionId));
    }

    addOpCode(OpCodes::op_call_native_function);
    addIndex(nativeFunctionId);

    const auto argumentCount = arguments->getArgumentCount();

    if (argumentCount != 0) {
        addFreeArguments(arguments);

        switch (argumentCount) {
        case 1:
            addOpCode(OpCodes::op_pop_1);
            break;

        case 2:
            addOpCode(OpCodes::op_pop_2);
            break;

        case 3:
            addOpCode(OpCodes::op_pop_3);
            break;

        default:
            addOpCode(OpCodes::op_pop_n);
            addIndex(argumentCount);
        }
    }

    // if (!nativeFunction.parameters.empty()) {
    //     for (auto i = 0; i < nativeFunction.parameters.size(); i++) {
    //         const auto& parameter = nativeFunction.parameters[i];
    //
    //         if (parameter.type == getRuntime()->getStringType()) {
    //             addOpCode(OpCodes::op_string_free_stack);
    //             addIndex(i);
    //         }
    //     }
    //
    //     addOpCode(OpCodes::op_pop_n);
    //     addIndex(arguments->getArgumentCount());
    // }

    return m_instructions.size() - 1;
}

void Compiler::setOpCode(const NomadIndex index, const NomadString& opCodeName) {
    NomadId opCodeId = m_runtime->getInstructionId(opCodeName);

    if (opCodeId == NOMAD_INVALID_ID) {
        reportInternalError("Unknown op code: " + opCodeName);
    }

    setOpCode(index, opCodeId);
}

void Compiler::setOpCode(NomadIndex index, NomadId opCodeId) {
    auto fn = m_runtime->getInstructionFn(opCodeId);

    if (fn == nullptr) {
        reportInternalError("Unknown op code: " + toString(opCodeId));
    }

    m_instructions[index].fn = fn;
}


void Compiler::setId(NomadIndex index, NomadId id) {
    m_instructions[index].value.setIdValue(id);
}

void Compiler::setIndex(NomadIndex index, NomadIndex value) {
    m_instructions[index].value.setIndexValue(value);
}

void Compiler::preParseFunction(CompilerContext* context, ScriptFile& scriptFile) {
    auto function = m_runtime->getFunction(scriptFile.functionId);
    auto& tokens = *scriptFile.tokens;

    tokens.reset();

    while (tokens.nextLine()) {
        auto& statement = tokens.next();

        PreParseStatementFn preParseStatementFn;

        if (getGetPreParseStatementFn(statement.textValue, preParseStatementFn) && preParseStatementFn) {
            // Errors are reported to the context; keep going to collect errors from the following lines.
            (void)preParseStatementFn(context, function, &tokens);
        }
    }
}


void Compiler::parseFunction(CompilerContext* context, ScriptFile& file) {
    auto ast = std::make_unique<FunctionNode>(0, 0);
    auto& tokens = *file.tokens;

    tokens.reset();

    // Statements with errors are skipped so the following lines are still parsed.
    while (tokens.nextLine()) {
        const auto function = m_runtime->getFunction(file.functionId);

        auto statement = parser::parseLine(context, function, &tokens);

        if (statement) {
            ast->addStatement(std::move(statement));
        }
    }

    ast->setEndSpan(tokens.getLineCount(), 0);

    setFunctionNode(file.functionId, std::move(ast));
}

bool Compiler::resolveFunction(CompilerContext* context, const FunctionSource& source) {
    return source.ast->resolve(context, source.function);
}

void Compiler::checkReturnPaths(CompilerContext* context, const FunctionSource& source) const {
    const auto* returnType = source.function->getReturnType();

    if (returnType == nullptr || returnType->isVoid() || source.ast->alwaysReturns()) {
        return;
    }

    context->reportError(
        "Missing `return` at the end of function '" + source.function->getName() + "'. It returns `" +
            returnType->getTypeName() + "`, so every path must end with a `return`",
        source.function->getPath(),
        source.ast->getEndLine(),
        source.ast->getEndColumn()
    );
}

void Compiler::inferTypes(CompilerContext* context) {
    // Return and variable types are inferred while resolving, so a function can depend on a type that is only discovered
    // by a function resolved after it. Resolve until no new type is discovered and discard the diagnostics: the final
    // resolve pass reports errors once, when every type that can be inferred is known.
    auto inferredTypes = collectInferredTypes();

    for (NomadIndex pass = 0; pass <= m_sources.size(); ++pass) {
        CompilerContext inferenceContext(this, context->getMode());

        for (const auto& source: m_sources) {
            try {
                (void)resolveFunction(&inferenceContext, source);
            } catch (const NomadBug&) {
                throw;
            } catch (const NomadException&) {
                // The final resolve pass reports it.
            }
        }

        auto updatedTypes = collectInferredTypes();

        if (updatedTypes == inferredTypes) {
            break;
        }

        inferredTypes = std::move(updatedTypes);
    }

    // Functions without a `return` statement return void.
    for (const auto& source: m_sources) {
        if (source.function->getReturnType() == nullptr && !m_returningFunctions.contains(source.function->getId())) {
            source.function->setReturnType(m_runtime->getVoidType());
        }
    }
}

std::vector<const Type*> Compiler::collectInferredTypes() const {
    std::vector<const Type*> types;

    for (const auto& source: m_sources) {
        types.push_back(source.function->getReturnType());

        for (NomadIndex variableIndex = 0; variableIndex < source.function->getVariableCount(); ++variableIndex) {
            types.push_back(source.function->getVariableType(toNomadId(variableIndex)));
        }
    }

    for (auto contextId = m_runtime->getFirstVariableContextId(); contextId != NOMAD_INVALID_ID;
         contextId = m_runtime->getNextVariableContextId(contextId)) {
        const auto* variableContext = m_runtime->getVariableContext(contextId);

        for (auto variableId = variableContext->getFirstVariableId(); variableId != NOMAD_INVALID_ID;
             variableId = variableContext->getNextVariableId(variableId)) {
            types.push_back(variableContext->getVariableType(variableId));
        }
    }

    return types;
}

void Compiler::compileFunction(const FunctionSource& source) {
    Function* function = source.function;

    function->setFunctionStart(m_instructions.size());

    source.ast->compile(this, function);

    // Unreachable for valid programs: `checkReturnPaths` rejects functions returning a value that can reach their end.
    // Kept as a safety net so a caller never takes ownership of whatever `r` last held.
    if (const auto* returnType = function->getReturnType(); returnType != nullptr && returnType->isString()) {
        addLoadStringValue("", ExpressionTarget::Result);
    }

    addFunctionReturn(function);

    function->setFunctionEnd(m_instructions.size());
}

void Compiler::addFunctionReturn(const Function* function) {
    const NomadIndex variableCount = function->getVariableCount();

    if (variableCount == 0) {
        addOpCode(OpCodes::op_return);

        return;
    }

    // Free owned variables before releasing the function's variable slots.
    for (auto i = NomadIndex{0}; i < variableCount; ++i) {
        addFreeValue(function->getVariableType(toNomadId(i))->getTypeOpCodes()->freeFunctionVariable, i);
    }

    addOpCode(OpCodes::op_return_n);
    addIndex(variableCount);
}

NomadId Compiler::registerScriptFile(
    const NomadString& functionName,
    const NomadString& fileName,
    const NomadString& source
) {
    const auto existingFunctionId = m_runtime->getFunctionId(functionName);

    if (existingFunctionId != NOMAD_INVALID_ID) {
        const auto* existingFunction = m_runtime->getFunction(existingFunctionId);

        m_loadErrors.push_back(LoadError {
            "Function name '" + functionName + "' is already used by '" + existingFunction->getPath() + "'",
            fileName,
        });

        return NOMAD_INVALID_ID;
    }

    auto scriptFile = ScriptFile {
        functionName,
        fileName,
        source,
        nullptr,
        NOMAD_INVALID_ID,
    };

    // Also register function so it is available for linking
    scriptFile.functionId = registerFunctionSource(functionName, fileName, source);

    m_files.emplace_back(std::move(scriptFile));

    // Sort functions alphabetically to ensure consistent order in compiling across systems.
    std::sort(m_files.begin(), m_files.end(), [](const ScriptFile& a, const ScriptFile& b) {
        return a.functionName < b.functionName;
    });

    return scriptFile.functionId;
}

NomadId Compiler::registerFunctionSource(
    const NomadString& functionName,
    const NomadString& fileName,
    const NomadString& source
) {
    auto functionId = m_runtime->registerFunction(functionName, fileName, source);

    if (functionId == NOMAD_INVALID_ID) {
        reportError("Could not register function '" + functionName + "' (" + fileName + ")");
    }

    auto function = m_runtime->getFunction(functionId);

    m_sources.emplace_back(FunctionSource {
        functionName,
        {},
        function
    });

    return functionId;
}

void Compiler::setFunctionNode(NomadId functionId, std::unique_ptr<FunctionNode> ast) {
    for (auto& source: m_sources) {
        if (source.function->getId() == functionId) {
            source.ast = std::move(ast);

            return;
        }
    }

    reportInternalError("Unknown function id: " + toString(functionId));
}

void Compiler::loadScriptsFromPath(const NomadString& path) {
    scanDirectoryForScripts(path, "", 10);
}

bool Compiler::compileFunctions(CompilerContext* context) {
    const auto errorCount = context->getErrorCount();
    const auto hasNewErrors = [context, errorCount] {
        return context->getErrorCount() != errorCount;
    };

    // A few user errors are still thrown (e.g. function registration failures); report them as diagnostics. Internal bugs
    // keep propagating.
    const auto reportExceptions = [context](const NomadString& sourceName, const auto& action) {
        try {
            action();
        } catch (const NomadBug&) {
            throw;
        } catch (const AstException& exception) {
            context->reportError(exception.what(), sourceName, exception.getLine(), exception.getColumn());
        } catch (const NomadException& exception) {
            context->reportError(exception.what(), sourceName, NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX);
        }
    };

    const auto finish = [this, &hasNewErrors] {
        // Tokenizers report to the caller's context, so they must not outlive this call.
        for (auto& scriptFile: m_files) {
            scriptFile.tokens.reset();
        }

        m_functionLinks.clear();
        m_functionCallDependencies.clear();
        m_returningFunctions.clear();

        return !hasNewErrors();
    };

    for (const auto& loadError: m_loadErrors) {
        context->reportError(loadError.message, loadError.sourceName, NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX);
    }

    m_loadErrors.clear();

    if (hasNewErrors()) {
        return finish();
    }

    log::info("Pre-Parsing functions");

    for (auto& scriptFile: m_files) {
        log::debug("Pre-Parsing '" + scriptFile.functionName + "' (" + scriptFile.fileName + ")");

        reportExceptions(scriptFile.fileName, [&] {
            scriptFile.tokens = std::make_unique<Tokenizer>(context, scriptFile.fileName, scriptFile.source);

            preParseFunction(context, scriptFile);
        });
    }

    // Each pass depends on the declarations collected by the previous one. Stop after a pass that reported errors so
    // they do not cascade into unrelated errors (e.g. unknown `fun` names or `fun` bodies parsed as top-level code).
    if (hasNewErrors()) {
        return finish();
    }

    log::info("Parsing functions");

    for (auto& scriptFile: m_files) {
        log::debug("Parsing '" + scriptFile.functionName + "' (" + scriptFile.fileName + ")");

        reportExceptions(scriptFile.fileName, [&] {
            parseFunction(context, scriptFile);
        });
    }

    if (hasNewErrors()) {
        return finish();
    }

    log::info("Syntax checking functions");

    reportExceptions({}, [&] {
        inferTypes(context);
    });

    for (const auto& source: m_sources) {
        log::debug("Syntax checking function '" + source.function->getName() + "' (" + source.function->getPath() + ")");

        reportExceptions(source.function->getPath(), [&] {
            (void)resolveFunction(context, source);
            checkReturnPaths(context, source);
            source.ast->checkReachability(context, source.function);
        });
    }

    if (hasNewErrors()) {
        return finish();
    }

    log::info("Compiling functions");

    for (auto& source: m_sources) {
        log::debug("Compiling function '" + source.function->getName() + "' (" + source.function->getPath() + ")");

        reportExceptions(source.function->getPath(), [&] {
            compileFunction(source);
        });
    }

    if (hasNewErrors()) {
        return finish();
    }

    log::info("Linking functions");

    reportExceptions({}, [&] {
        linkFunctions();
    });

    log::info("Verification");

    postCompile(context);

    if (!hasNewErrors()) {
        log::info("Functions compiled");
    }

    return finish();
}
void Compiler::addFunctionLink(NomadId functionId, NomadIndex callIndex) {
    m_functionLinks.emplace_back(FunctionLink{functionId, callIndex});
}

void Compiler::addFunctionCallDependency(const NomadId callerFunctionId, const NomadId calleeFunctionId) {
    m_functionCallDependencies[callerFunctionId].insert(calleeFunctionId);
}

bool Compiler::isRecursiveFunctionCall(const NomadId callerFunctionId, const NomadId calleeFunctionId) const {
    std::unordered_set<NomadId> visited;
    std::vector<NomadId> pending = { calleeFunctionId };

    while (!pending.empty()) {
        const auto functionId = pending.back();
        pending.pop_back();

        const auto dependencies = m_functionCallDependencies.find(functionId);

        if (dependencies == m_functionCallDependencies.end()) {
            continue;
        }

        for (const auto dependencyId: dependencies->second) {
            if (dependencyId == callerFunctionId || dependencyId == calleeFunctionId) {
                return true;
            }

            if (visited.insert(dependencyId).second) {
                pending.push_back(dependencyId);
            }
        }
    }

    return callerFunctionId == calleeFunctionId;
}

void Compiler::addReturningFunction(const NomadId functionId) {
    m_returningFunctions.insert(functionId);
}

void Compiler::linkFunctions() {
    for (auto& link: m_functionLinks) {
        auto function = m_runtime->getFunction(link.functionId);

        if (function == nullptr) {
            reportInternalError("Failed to link function '" + toString(link.functionId) + "'");
        }
        if (function->getFunctionStart() == NOMAD_INVALID_INDEX) {
            reportInternalError("Function '" + function->getName() + "' has not been compiled");
        }

        setIndex(link.callIndex, function->getFunctionStart());
    }
}

void Compiler::postCompile(CompilerContext* compilerContext) {
    // verify that all variables have been initialized and are used
    auto contextId = m_runtime->getFirstVariableContextId();

    while (contextId != NOMAD_INVALID_ID) {
        const auto context = m_runtime->getVariableContext(contextId);

        auto variableId = context->getFirstVariableId();

        while (variableId != NOMAD_INVALID_ID) {
            const auto variableName = context->getVariableName(variableId);

            if (!context->isWritten(variableId)) {
                compilerContext->reportError("Variable '" + variableName + "' has not been initialized");
            }

            if (!context->isRead(variableId)) {
                log::debug("Variable '" + variableName + "' has been declared but not used");
            }

            variableId = context->getNextVariableId(variableId);
        }

        contextId = m_runtime->getNextVariableContextId(contextId);
    }
}

void Compiler::scanDirectoryForScripts(const NomadString& basePath, const NomadString& sub_path, NomadIndex max_depth) {
    if (max_depth == 0) {
        log::debug("Skipping directory '" + sub_path + "' (max depth reached)");

        return;
    }

    const NomadString extension = ".nomad";

    auto path_string = concatPath(basePath, sub_path);
    std::filesystem::path path{path_string};

    std::error_code iteratorError;
    auto iterator = std::filesystem::directory_iterator(
        path,
        std::filesystem::directory_options::skip_permission_denied,
        iteratorError
    );

    if (iteratorError) {
        m_loadErrors.push_back(LoadError{
            "Failed to scan directory '" + path.generic_string() + "': " + iteratorError.message(),
            path.generic_string()
        });
        return;
    }

    const auto end = std::filesystem::directory_iterator();

    while (iterator != end) {
        const auto entry = *iterator;
        std::error_code statusError;

        if (entry.is_symlink(statusError)) {
            log::debug("Skipping symbolic link '" + entry.path().generic_string() + "'");
        } else if (statusError) {
            log::debug(
                "Skipping inaccessible path '" + entry.path().generic_string() + "': " + statusError.message()
            );
        } else if (entry.is_directory(statusError)) {
            auto directory_name = NomadString(entry.path().filename().string());
            auto new_sub_path = concatPath(sub_path, directory_name);
            scanDirectoryForScripts(basePath, new_sub_path, max_depth - 1);
        } else if (statusError) {
            log::debug(
                "Skipping inaccessible path '" + entry.path().generic_string() + "': " + statusError.message()
            );
        } else if (entry.is_regular_file(statusError) && entry.path().extension() == extension) {
            auto file_name = basePath + "/" + sub_path + "/" + NomadString(entry.path().filename().string());

            auto start = basePath.length() + 1;
            auto length = entry.path().string().length() - start - extension.length();

            auto function_name = NomadString(entry.path().string().substr(start, length));
            std::replace(function_name.begin(), function_name.end(), '\\', '.');
            std::replace(function_name.begin(), function_name.end(), '/', '.');

            // Read source
            std::ifstream file(file_name.c_str(), std::ios::in);
            if (!file.is_open()) {
                m_loadErrors.push_back(LoadError {"Failed to open file '" + file_name + "'", file_name});

                continue;
            }

            NomadString source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

            registerScriptFile(function_name, file_name, source);
        } else if (statusError) {
            log::debug(
                "Skipping inaccessible path '" + entry.path().generic_string() + "': " + statusError.message()
            );
        }

        iterator.increment(iteratorError);

        if (iteratorError) {
            log::debug(
                "Failed to continue scanning directory '" + path.generic_string() + "': " + iteratorError.message()
            );
            break;
        }
    }
}

NomadString Compiler::generateFunctionName(const NomadString& generatedName, const NomadString& hostFunctionName, NomadIndex line) {
    NomadString generatedFunctionName;
    auto index = NomadIndex{0};

    do {
        generatedFunctionName = FUNCTION_INTERNAL_NAME_PREFIX + generatedName + "_" + hostFunctionName + "_" + std::to_string(line) + "_" + std::to_string(index);
        ++index;
    } while (m_runtime->getFunctionId(generatedFunctionName) != NOMAD_INVALID_ID);

    return generatedFunctionName;
}

NomadString Compiler::generateFunctionName(const NomadString& name, const Function* function, NomadIndex line) {
    return generateFunctionName(name, function->getName(), line);
}

} // nomad
