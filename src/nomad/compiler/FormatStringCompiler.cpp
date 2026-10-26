// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/FormatStringCompiler.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/Expression.hpp>

#include <cctype>

namespace nomad {

namespace {

enum class DelimiterType {
    FormatString = 1,
    Placeholder,
    String,
};

std::optional<NomadIndex> findDelimiterEnd(
    const NomadStringView source,
    const NomadIndex start,
    const DelimiterType type
) {
    std::vector<DelimiterType> delimiters{type};

    for (auto index = start; index < source.size(); ++index) {
        const auto character = source[index];
        const auto delimiter = delimiters.back();

        if (delimiter != DelimiterType::Placeholder && character == '\\') {
            if (index + 1 < source.size()) {
                const auto escaped = source[index + 1];
                if (delimiter == DelimiterType::String || escaped == '"' || escaped == '\\' ||
                    escaped == 'n' || escaped == 'r' || escaped == 't') {
                    ++index;
                }
            }
            continue;
        }

        if (delimiter == DelimiterType::Placeholder) {
            if (character == '$' && index + 1 < source.size() && source[index + 1] == '"') {
                delimiters.push_back(DelimiterType::FormatString);
                ++index;
            } else if (character == '"') {
                delimiters.push_back(DelimiterType::String);
            } else if (character == '{') {
                delimiters.push_back(DelimiterType::Placeholder);
            } else if (character == '}') {
                delimiters.pop_back();
            }
        } else if (character == '"') {
            delimiters.pop_back();
        } else if (delimiter == DelimiterType::FormatString && character == '{') {
            delimiters.push_back(DelimiterType::Placeholder);
        }

        if (delimiters.empty()) {
            return index;
        }
    }

    return std::nullopt;
}

} // namespace

FormatTemplate parseFormatTemplate(const NomadStringView format) {
    FormatTemplate parsed;
    auto index = NomadIndex{0};

    while (index < format.size()) {
        const auto start = index;
        while (index < format.size() && format[index] != '{') {
            ++index;
        }

        if (index > start) {
            parsed.segments.push_back({
                FormatTemplateSegmentType::Literal,
                NomadString(format.substr(start, index - start)),
                start,
                index - start,
                std::nullopt,
            });
        }

        if (index == format.size()) {
            break;
        }

        const auto placeholderStart = index;
        const auto end = findDelimiterEnd(format, placeholderStart + 1, DelimiterType::Placeholder);
        const auto length = end ? *end - placeholderStart + 1 : format.size() - placeholderStart;
        const auto text = format.substr(placeholderStart + 1, end ? length - 2 : length - 1);
        auto empty = true;
        for (const auto character : text) {
            if (!std::isspace(static_cast<unsigned char>(character))) {
                empty = false;
                break;
            }
        }

        std::optional<NomadIndex> diagnosticIndex;
        if (!end || empty) {
            diagnosticIndex = parsed.diagnostics.size();
            parsed.diagnostics.push_back({
                end ? "Empty placeholder in format string" : "Missing '}' in format string",
                placeholderStart,
                length,
            });
        }

        parsed.segments.push_back({
            diagnosticIndex ? FormatTemplateSegmentType::Error : FormatTemplateSegmentType::Placeholder,
            NomadString(text),
            placeholderStart,
            length,
            diagnosticIndex,
        });

        index = placeholderStart + length;
    }

    return parsed;
}

NomadString decodeFormatStringLiteral(const NomadStringView content) {
    NomadString decoded;
    decoded.reserve(content.size());
    const auto parsed = parseFormatTemplate(content);

    for (const auto& segment : parsed.segments) {
        const auto segmentEnd = segment.offset + segment.length;

        for (auto index = segment.offset; index < segmentEnd; ++index) {
            auto character = content[index];

            if (segment.type == FormatTemplateSegmentType::Literal && character == '\\' && index + 1 < segmentEnd) {
                const auto escaped = content[index + 1];
                switch (escaped) {
                    case '"': character = '"'; ++index; break;
                    case '\\': character = '\\'; ++index; break;
                    case 'n': character = '\n'; ++index; break;
                    case 'r': character = '\r'; ++index; break;
                    case 't': character = '\t'; ++index; break;
                    default: break;
                }
            }

            decoded.push_back(character);
        }
    }

    return decoded;
}

std::optional<NomadIndex> findFormatStringLiteralEnd(const NomadStringView source, const NomadIndex offset) {
    if (offset >= source.size() || source.size() - offset < 2 || source.substr(offset, 2) != "$\"") {
        throw NomadBug("Format string literal offset must point at '$\"'");
    }

    return findDelimiterEnd(source, offset + 2, DelimiterType::FormatString);
}

std::optional<NomadString> compileFormatString(
    const Compiler* compiler,
    ScriptCallableBase* function,
    const NomadString& format,
    FormatString* formatString
) {
    const auto parsed = parseFormatTemplate(format);
    for (const auto& segment : parsed.segments) {
        if (segment.type == FormatTemplateSegmentType::Error) {
            return parsed.diagnostics[*segment.diagnosticIndex].message;
        } else if (segment.type == FormatTemplateSegmentType::Literal) {
            if (formatString != nullptr) {
                formatString->addLiteral(segment.text);
            }
        } else {
            const auto variableName = segment.text;
            if (compiler->isFieldInitializer(function) &&
                (variableName == "this" || variableName.starts_with("this."))) {
                return "Field initializers cannot use or capture 'this'";
            }
            IdentifierDefinition identifier{};

            if (resolveIdentifier(variableName, compiler, function, identifier) == false) {
                return "Unknown identifier in format string: " + variableName;
            }

            switch (identifier.identifierType) {
                case IdentifierType::Function: {
                    const auto placeholderFunction = compiler->getRuntime()->getScriptCallable(identifier.functionId);

                    if (placeholderFunction != nullptr && placeholderFunction->getParameterCount() != 0) {
                        return "Cannot use function with parameters in format string: " + variableName;
                    }

                    if (formatString != nullptr && identifier.valueType == nullptr) {
                        return "Cannot determine return type of function in format string: " + variableName;
                    }

                    break;
                }

                case IdentifierType::Constant:
                case IdentifierType::DynamicVariable:
                case IdentifierType::ContextVariable:
                case IdentifierType::FunctionVariable:
                case IdentifierType::Parameter:
                case IdentifierType::Receiver:
                    // All the above are OK. Continue
                    break;

                case IdentifierType::Keyword:
                    return "Cannot use keyword in format string: " + variableName;

                case IdentifierType::NativeFunction:
                    return "Cannot use native function in format string: " + variableName;

                case IdentifierType::Statement:
                    return "Cannot use statement in format string: " + variableName;

                case IdentifierType::Event:
                    return "Cannot use event type in format string: " + variableName;

                default:
                    return "Unknown identifier in format string: " + variableName;
            }

            if (formatString != nullptr) {
                if (identifier.identifierType == IdentifierType::Function) {
                    formatString->addFunction(identifier.valueType, identifier.functionId);
                } else if (identifier.identifierType == IdentifierType::Constant) {
                    formatString->addVariable(identifier.identifierType, identifier.valueType, identifier.constantId.id);
                } else if (identifier.identifierType == IdentifierType::DynamicVariable) {
                    formatString->addVariable(identifier.identifierType, identifier.valueType, identifier.dynamicVariableId.id);
                } else if (identifier.identifierType == IdentifierType::ContextVariable) {
                    formatString->addVariable(identifier.identifierType, identifier.valueType, identifier.contextVariableId.id);
                } else if (identifier.identifierType == IdentifierType::FunctionVariable) {
                    formatString->addVariable(identifier.identifierType, identifier.valueType, identifier.variableId.id);
                } else if (identifier.identifierType == IdentifierType::Receiver) {
                    formatString->addVariable(identifier.identifierType, identifier.valueType, NOMAD_INVALID_ID);
                } else {
                    formatString->addVariable(
                        identifier.identifierType,
                        identifier.valueType,
                        identifier.parameterId
                    );
                }
            }
        }
    }

    return std::nullopt;
}

} // nomad
