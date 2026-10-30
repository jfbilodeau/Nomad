// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/InstructionDump.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/FormatString.hpp>
#include <nomad/script/NomadStringObject.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>

#include <iomanip>
#include <ostream>
#include <vector>

namespace nomad {

namespace {

NomadString getQualifiedCallableName(const Callable* callable) {
    const auto& name = callable->getName();
    if (callable->getType() != CallableType::Method) {
        return name;
    }
    const auto* owner = callable->getClassType();
    if (name.starts_with(owner->getTypeName() + ".") || name.starts_with(owner->getTypeName() + "$")) return name;
    return owner->getTypeName() + "." + name;
}

void dumpCallableDeclaration(const Callable* callable, std::ostream& stream) {
    const auto type = callable->getType();
    stream << (type == CallableType::Function ? "function " :
        type == CallableType::Method ? "method " : "constructor ") << getQualifiedCallableName(callable);
    for (const auto& parameter : callable->getParameters()) {
        stream << " " << parameter.name << ":" << parameter.getTypeName();
    }
    stream << std::endl;
}

} // namespace

void dumpInstructions(const Runtime& runtime, std::ostream& stream) {
    std::vector<const ScriptCallableBase*> callables;
    runtime.getScriptCallables(callables);
    for (const auto* callable : callables) {
        if (callable->getFunctionStart() != NOMAD_INVALID_INDEX && callable->getFunctionEnd() != NOMAD_INVALID_INDEX) {
            dumpInstructions(runtime, *callable, stream);
        }
    }
}

void dumpInstructions(const Runtime& runtime, const ScriptCallableBase& callable, std::ostream& stream) {
    dumpCallableDeclaration(runtime.getCallable(callable.getId()), stream);
    const auto& instructions = runtime.getInstructions();
    for (auto index = callable.getFunctionStart(); index < callable.getFunctionEnd(); ++index) {
        const auto instructionId = runtime.getInstructionId(instructions[index].fn);
        if (instructionId.isInvalid()) {
            stream << std::setfill('0') << std::setw(6) << index << ": Unknown instruction" << std::endl;
            continue;
        }
        const auto& name = runtime.getInstructionName(instructionId);
        const auto& operands = runtime.getInstructionOperands(instructionId);
        stream << std::setfill('0') << std::setw(6) << index << ": " << name << std::endl;

        for (const auto operand : operands) {
            ++index;
            const auto& value = instructions[index].value;
            NomadString textValue;
            switch (operand) {
                case Operand::Boolean:
                    textValue = value.getBooleanValue() ? "true" : "false";
                    break;
                case Operand::Integer:
                    textValue = std::to_string(value.getIntegerValue());
                    break;
                case Operand::Float:
                    textValue = std::to_string(value.getFloatValue());
                    break;
                case Operand::String:
                    textValue = "\"" + runtime.getString(StringId{value.getIdValue()})->getString() + "\" (" +
                        std::to_string(value.getIdValue()) + ")";
                    break;
                case Operand::Id:
                    textValue = std::to_string(value.getIdValue());
                    break;
                case Operand::Function: {
                    const auto functionStart = value.getIndexValue();
                    const auto* function = runtime.getScriptCallableByStartAddress(functionStart);
                    if (function == nullptr) {
                        throw NomadException("Cannot dump instructions: unresolved script target at " +
                            std::to_string(functionStart));
                    }
                    textValue = getQualifiedCallableName(runtime.getCallable(function->getId())) + " (" +
                        std::to_string(function->getId().id) + ")";
                    break;
                }
                case Operand::NativeFunction: {
                    const auto* function = runtime.getNativeFunction(CallableId{value.getIdValue()});
                    if (function != nullptr) {
                        textValue = function->getName() + " (" + std::to_string(function->getId().id) + ")";
                    } else {
                        textValue = "<unknown native function> (" + std::to_string(value.getIdValue()) + ")";
                    }
                    break;
                }
                case Operand::FunctionVariable:
                    textValue = callable.getVariableName(VariableId{value.getIdValue()}) + " (" +
                        std::to_string(value.getIdValue()) + ")";
                    break;
                case Operand::DynamicVariable:
                    textValue = runtime.getDynamicVariableName(DynamicVariableId{value.getIdValue()}) + " (" +
                        std::to_string(value.getIdValue()) + ")";
                    break;
                case Operand::ContextVariableId:
                    textValue = runtime.getContextVariableName(ContextVariableId{value.getIdValue()}) + " (" +
                        std::to_string(value.getIdValue()) + ")";
                    break;
                case Operand::FormatString:
                    textValue = "$\"" + runtime.getFormatString(FormatStringId{value.getIdValue()})->getFormatString() +
                        "\" (" + std::to_string(value.getIdValue()) + ")";
                    break;
                case Operand::Index:
                    textValue = std::to_string(value.getIndexValue());
                    break;
                default:
                    textValue = "unknown (" + std::to_string(static_cast<int>(operand)) + ")";
                    log::debug("Unknown operand type: " + std::to_string(static_cast<int>(operand)));
            }
            stream << std::setfill('0') << std::setw(6) << index << ": " << textValue << std::endl;
        }
        stream.flush();
    }
}

} // namespace nomad
