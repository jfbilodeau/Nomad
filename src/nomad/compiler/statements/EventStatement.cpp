// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/statements/EventStatement.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

namespace nomad {

bool preParseEventStatement(CompilerContext* context, ScriptFunction* /*function*/, Tokenizer* tokens) {
    if (tokens->endOfLine()) {
        context->reportError("Event name expected", tokens);

        return false;
    }

    const auto eventName = context->qualifyDeclaration(tokens->currentToken().textValue);
    if (context->getRuntime()->isHub(eventName) || context->getRuntime()->isHubVariable(eventName)) {
        context->reportError("Event name '" + eventName + "' is reserved by a hub declaration", tokens);
        return false;
    }

    // Make sure event is not already declared
    if (context->getRuntime()->getEventId(eventName) != NOMAD_INVALID_ID) {
        context->reportError("Event '" + eventName + "' is already declared", tokens);

        return false;
    }

    tokens->next(); // Consume event name

    std::vector<EventParameter> parameters;

    while (tokens->endOfLine() == false) {
        const auto parameterName = tokens->currentToken().textValue;

        tokens->next(); // Consume parameter name

        if (!tokens->expect(":")) {
            return false;
        }

        if (tokens->endOfLine()) {
            context->reportError("Parameter type expected", tokens);

            return false;
        }

        const auto* type = parser::parseType(context, tokens);
        if (type == nullptr) {
            return false;
        }

        const auto typeName = type->getTypeName();
        const auto typeId = context->getRuntime()->getTypeId(typeName);
        parameters.emplace_back(parameterName, typeId, typeName);
    }

    const auto eventId = context->getRuntime()->registerEvent(eventName, parameters);

    if (eventId == NOMAD_INVALID_ID) {
        context->reportError("Failed to register event '" + eventName + "'", tokens);

        return false;
    }

    return true;
}

} // namespace nomad
