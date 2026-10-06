// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/FormatString.hpp>

#include <nomad/system/String.hpp>

#include <nomad/script/Runtime.hpp>

namespace nomad {

FormatString::FormatString(const NomadString& formatString, NomadId functionId):
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
    literal
    });
}

void FormatString::addVariable(const IdentifierType variable_type, const Type* valueType, NomadId variableId) {
    m_segments.push_back({
        SegmentType::Variable,
        variable_type,
        valueType,
        variableId,
        "",
    });
}

NomadString FormatString::format(VirtualMachine *interpreter) const {
    NomadString string;

    for (auto const& segment: m_segments) {
        switch (segment.type) {
            case SegmentType::Literal: {
                string += segment.value;
                break;
            }
            case SegmentType::Variable: {
                RuntimeValue value;

                if (segment.variableType == IdentifierType::Function) {
                    const auto function = interpreter->getRuntime()->getFunction(segment.variableId);

                    if (function == nullptr) {
                        interpreter->fault("Internal error: Unknown function in format string segment");
                    }

                    // Running the function clobbers the registers, which may hold operands of the enclosing expression.
                    const auto savedResult = interpreter->getResult();
                    const auto savedIntermediate = interpreter->getIntermediate();

                    interpreter->run(function, {});

                    value = interpreter->getResult();
                    string += segment.valueType->toString(value);
                    segment.valueType->freeValue(value);

                    interpreter->setResult(savedResult);
                    interpreter->setIntermediate(savedIntermediate);

                    break;
                }

                const auto result = interpreter->getVariableValue(
                    segment.variableType,
                    segment.variableId,
                    value
                );

                if (!result) {
                    interpreter->fault(
                        "Failed to get variable value for format string segment"
                    );
                }

                string += segment.valueType->toString(value);

                // Dynamic getters hand over an owned value; other variables are shared.
                if (segment.variableType == IdentifierType::DynamicVariable) {
                    segment.valueType->freeValue(value);
                }

                break;
            }
            default:
                interpreter->fault(
                    "Internal error: Invalid format string segment type: " + toString((int) segment.type)
                );
        }
    }

    return string;
}

} // nomad
