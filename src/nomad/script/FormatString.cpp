// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/FormatString.hpp>

#include <nomad/compiler/FormatStringCompiler.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>

#include <nomad/system/String.hpp>

#include <algorithm>

namespace nomad {

DynamicFormatResult formatDynamicString(const NomadStringView format, InterpreterContext& context,
    const NomadStringView sourceName, const NomadIndex line, const NomadIndex column) {
    DynamicFormatResult result;
    const auto parsed = parseFormatTemplate(format);
    const Interpreter interpreter;
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
        } else if (interpreter.evaluateExpression(
            SourceText{NomadString{sourceName}, segment.text}, context, line
        )) {
            const auto* value = context.getResult();
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

void FormatString::addExpression(const Type* valueType, const CallableId functionId) {
    m_segments.push_back({
        SegmentType::Expression,
        IdentifierType::Function,
        valueType,
        NOMAD_INVALID_ID,
        functionId,
        "",
    });
}

NomadString FormatString::format(VirtualMachine *interpreter) const {
    NomadString string;

    const auto appendFunction = [&](const Segment& segment, const bool captureFrame) {
        const auto* function = interpreter->getRuntime()->getScriptFunction(segment.functionId);
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
