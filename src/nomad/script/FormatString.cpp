// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/FormatString.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>

#include <nomad/system/String.hpp>

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

void FormatString::addExpression(const Type* valueType, const NomadId functionId) {
    m_segments.push_back({
        SegmentType::Expression,
        IdentifierType::Function,
        valueType,
        functionId,
        "",
    });
}

NomadString FormatString::format(VirtualMachine *interpreter) const {
    NomadString string;

    const auto appendFunction = [&](const Segment& segment, const bool captureFrame) {
        const auto* function = interpreter->getRuntime()->getFunction(segment.variableId);
        if (function == nullptr || segment.valueType == nullptr || segment.valueType->isVoid() ||
            function->getReturnType() != segment.valueType) {
            interpreter->fault("Internal error: Invalid function in format string segment");
        }

        const auto captures = captureFrame ? interpreter->captureCurrentFrame(function) : std::vector<TypedValue>{};
        std::vector<RuntimeValue> values;
        values.reserve(captures.size());
        for (const auto& capture : captures) {
            values.push_back(capture.getValue());
        }

        // Nested execution must not discard operands held by the enclosing expression.
        const auto savedResult = interpreter->getResult();
        const auto savedIntermediate = interpreter->getIntermediate();
        try {
            interpreter->run(function, {}, values);
            auto value = interpreter->getResult();
            try {
                string += segment.valueType->toString(value);
            } catch (...) {
                segment.valueType->freeValue(value);
                throw;
            }
            segment.valueType->freeValue(value);
        } catch (...) {
            interpreter->setResult(savedResult);
            interpreter->setIntermediate(savedIntermediate);
            throw;
        }
        interpreter->setResult(savedResult);
        interpreter->setIntermediate(savedIntermediate);
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
                    interpreter->fault("Internal error: Invalid value type in format string segment");
                }

                if (segment.variableType == IdentifierType::Function) {
                    appendFunction(segment, false);
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
            case SegmentType::Expression: {
                appendFunction(segment, true);
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
