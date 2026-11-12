// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/FormatString.hpp>

#include <nomad/interpreter/InterpreterCallFrame.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/RuntimeValueGuard.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <nomad/system/String.hpp>

#include <boost/scope_exit.hpp>

#include <optional>

namespace nomad {

FormatString::FormatString(const NomadString& formatString, CallableId functionId):
    m_formatString(formatString),
    m_functionId(functionId)
{}

FormatString::~FormatString() = default;

void FormatString::addLiteral(const NomadString& literal) {
    m_segments.push_back({
    SegmentType::Literal,
    IdentifierType::Unknown,
    nullptr,
    NOMAD_INVALID_ID,
    CallableId{},
    literal
    });
}

void FormatString::addVariable(const IdentifierType variable_type, const Type* valueType, NomadId variableId) {
    m_segments.push_back({
        SegmentType::Variable,
        variable_type,
        valueType,
        variableId,
        CallableId{},
        "",
    });
}

void FormatString::addFunction(const Type* valueType, const CallableId functionId) {
    m_segments.push_back({
        SegmentType::Variable,
        IdentifierType::ScriptFunction,
        valueType,
        NOMAD_INVALID_ID,
        functionId,
        "",
    });
}

void FormatString::addExpression(const Type* valueType, const CallableId functionId) {
    m_segments.push_back({
        SegmentType::Expression,
        IdentifierType::ScriptFunction,
        valueType,
        NOMAD_INVALID_ID,
        functionId,
        "",
    });
}

NomadString FormatString::format(VirtualMachine* machine) const {
    NomadString string;

    const auto appendFunction = [&](const Segment& segment) {
        const auto* function = machine->getRuntime()->getScriptFunction(segment.functionId);
        if (
            function == nullptr ||
            segment.valueType == nullptr ||
            segment.valueType->isVoid() ||
            function->getReturnType() != segment.valueType ||
            function->getFunctionStart() == NOMAD_INVALID_INDEX ||
            function->getFunctionStart() >= function->getFunctionEnd()
        ) {
            machine->fault("Internal error: Invalid function in format string segment");
        }

        InterpreterCallFrame frame(*machine, *function);
        const auto state = machine->captureExecutionState();
        {
            BOOST_SCOPE_EXIT_ALL(&) {
                machine->restoreExecutionState(state);
            };

            if (!machine->executeFunction(*function)) {
                machine->fault("Format expression calls cannot yield");
            }
        }

        string += segment.valueType->toString(frame.getResult());
    };

    for (auto const& segment: m_segments) {
        switch (segment.type) {
            case SegmentType::Literal: {
                string += segment.value;
                break;
            }
            case SegmentType::Variable: {
                RuntimeValue value;
                if (segment.valueType == nullptr || segment.valueType->isVoid()) {
                    machine->fault("Internal error: Invalid value type in format string segment");
                }

                if (segment.variableType == IdentifierType::ScriptFunction) {
                    appendFunction(segment);
                    break;
                }

                std::optional<RuntimeValueGuard> valueGuard;
                if (segment.variableType == IdentifierType::DynamicVariable) {
                    valueGuard.emplace(*segment.valueType, &value);
                }

                const auto* function = machine->getCurrentCallable();
                const auto size = segment.valueType->getSize();
                const RuntimeValue* storage = nullptr;
                switch (segment.variableType) {
                    case IdentifierType::FunctionVariable:
                        if (function == nullptr) {
                            machine->fault("Format variable requires an active script frame");
                        }

                        storage = machine->getFunctionVariableStorage(VariableId{segment.variableId}).data();
                        break;

                    case IdentifierType::Parameter:
                        if (function == nullptr) {
                            machine->fault("Format parameter requires an active script frame");
                        }

                        storage = machine->getParameterStorage(
                            function->getParameterOffset(segment.variableId),
                            size
                        ).data();
                        break;

                    case IdentifierType::Receiver:
                        if (function == nullptr) {
                            machine->fault("Format receiver requires an active script frame");
                        }

                        if (segment.valueType->isStruct()) {
                            if (size != 0) {
                                const auto layout = function->getCallStorageLayout();
                                storage = machine->getParameterStorage(*layout.receiverOffset, 1)[0].getStorageAddress();
                            }
                        } else {
                            value.setObjectValue(machine->getScriptReceiver());
                            storage = &value;
                        }

                        break;

                    default:
                        if (size > 1) {
                            machine->fault("Format lookup does not provide complete value storage");
                        }

                        if (!machine->getVariableValue(segment.variableType, segment.variableId, value)) {
                            machine->fault("Failed to get variable value for format string segment");
                        }

                        storage = size == 0 ? nullptr : &value;
                        break;
                }

                string += segment.valueType->toString(storage);

                break;
            }
            case SegmentType::Expression: {
                appendFunction(segment);
                break;
            }
            default:
                machine->fault(
                    "Internal error: Invalid format string segment type: " + toString((int) segment.type)
                );
        }
    }

    return string;
}

} // nomad
