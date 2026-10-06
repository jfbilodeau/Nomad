// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/FormatStringCompiler.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/Expression.hpp>

namespace nomad {

std::optional<NomadString> compileFormatString(
    const Compiler* compiler,
    Function* function,
    const NomadString& format,
    FormatString* formatString
) {
    const NomadIndex length = format.length();
    NomadIndex index = 0;

    while (index < length) {
        NomadIndex start = index;

        while (index < length && format[index] != '{') {
            index++;
        }

        if (index > start) {
            if (formatString != nullptr) {
                formatString->addLiteral(format.substr(start, index - start));
            }
        }

        if (index < length && format[index] == '{') {
            index++;
            start = index;

            auto end = start;

            while (end < length && format[end] != '}') {
                end++;
            }

            if (end >= length) {
                return "Missing '}' in format string";
            }

            auto variableName = format.substr(start, end - start);

            IdentifierDefinition identifier{};

            if (resolveIdentifier(variableName, compiler, function, identifier) == false) {
                return "Unknown identifier in format string: " + variableName;
            }

            // compiler->getIdentifierDefinition(variableName, function, identifier);

            switch (identifier.identifierType) {
                case IdentifierType::Function: {
                    const auto placeholderFunction = compiler->getRuntime()->getFunction(identifier.variableId);

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
                formatString->addVariable(
                    identifier.identifierType,
                    identifier.valueType,
                    identifier.variableId
                );
            }

            index = end + 1;
        }
    }

    return std::nullopt;
}

} // nomad
