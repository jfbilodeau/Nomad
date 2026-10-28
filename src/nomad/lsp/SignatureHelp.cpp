// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/lsp/SignatureHelp.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/Type.hpp>

#include <boost/json.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace nomad {
namespace {

namespace json = boost::json;

bool isSourceParameter(const Runtime& runtime, const Type* type) {
    const auto shape = runtime.getParameterShape(type);
    return shape == ParameterShape::SourceFile || shape == ParameterShape::SourceFunction ||
        shape == ParameterShape::SourceLine;
}

std::vector<const Parameter*> visibleParameters(const Runtime& runtime, const Callable& callable) {
    std::vector<const Parameter*> parameters;
    for (const auto& parameter : callable.getParameters()) {
        if (!isSourceParameter(runtime, parameter.type)) parameters.push_back(&parameter);
    }
    return parameters;
}

NomadString typeName(const Type* type) {
    return type ? type->getTypeName() : "unknown";
}

NomadString callableLabel(const Runtime& runtime, const Callable& callable) {
    auto label = callable.getName();
    if (callable.getType() == CallableType::Constructor) {
        label = "new " + label;
    } else if (callable.getType() == CallableType::Method) {
        label = callable.getClassType()->getTypeName() + "." + label;
    }
    for (const auto* parameter : visibleParameters(runtime, callable)) {
        label += " " + parameter->name + ":" + typeName(parameter->type);
    }
    const auto* constructor = runtime.getConstructor(callable.getId());
    return label + " -> " + typeName(constructor ? constructor->getConstructedType() : callable.getReturnType());
}

std::vector<const Callable*> findCallables(
    Runtime& runtime,
    CompilerContext& context,
    Tokenizer& tokens,
    const std::function<const Type*(const NomadString&)>& findValueType
) {
    std::vector<const Callable*> callables;
    const auto name = tokens.next().textValue;
    if (name == "new") {
        if (!tokens.is(TokenType::Identifier)) return {};
        const Type* type = nullptr;
        const auto constructorName = tokens.currentToken().textValue;
        if (const auto count = runtime.getParameterizedConstructorArgumentCount(constructorName)) {
            tokens.next();
            if (!tokens.expect("<")) return {};
            std::vector<const Type*> arguments;
            while (!tokens.endOfLine() && !tokens.is(">")) {
                const auto* argument = parser::parseType(&context, &tokens);
                if (argument == nullptr) return {};
                arguments.push_back(argument);
            }
            if (!tokens.expect(">") || arguments.size() != *count) return {};
            type = runtime.getTypeByName(runtime.getParameterizedConstructorName(constructorName, arguments));
        } else {
            type = parser::parseType(&context, &tokens);
        }
        const auto* object = type ? type->asClassType() : nullptr;
        if (object == nullptr) return {};
        for (const auto* constructor : object->getConstructors()) callables.push_back(constructor);
    } else {
        std::vector<CallableId> overloads;
        runtime.getCallableOverloads(name, overloads);
        for (const auto id : overloads) callables.push_back(runtime.getFunction(id));
        if (callables.empty()) {
            const auto separator = name.rfind('.');
            if (separator == NomadString::npos) return {};
            const auto* receiver = findValueType(name.substr(0, separator));
            const auto member = name.substr(separator + 1);
            for (auto index = NomadIndex{0}; index < runtime.getFunctionTableSize(); ++index) {
                const auto* method = runtime.getMethod(CallableId{toNomadId(index)});
                if (method == nullptr || method->getClassType() != receiver || method->getName() != member) continue;
                const auto* object = receiver->asClassType();
                if (object != nullptr && member == object->getInitializerName()) continue;
                callables.push_back(method);
            }
        }
    }
    std::ranges::sort(callables, [&runtime](const auto* left, const auto* right) {
        return callableLabel(runtime, *left) < callableLabel(runtime, *right);
    });
    return callables;
}

json::value describeSignatures(
    const Runtime& runtime,
    const std::vector<const Callable*>& callables,
    const std::vector<const Type*>& argumentTypes,
    const std::size_t activeParameter
) {
    json::array signatures;
    auto selected = std::size_t{0};
    auto bestScore = std::numeric_limits<std::size_t>::max();
    for (const auto* callable : callables) {
        const auto parameters = visibleParameters(runtime, *callable);
        json::array parameterInformation;
        auto score = std::size_t{0};
        auto compatible = parameters.size() > activeParameter || parameters.empty();
        for (auto index = std::size_t{0}; index < parameters.size(); ++index) {
            const auto* parameter = parameters[index];
            parameterInformation.push_back(json::object{
                {"label", parameter->name + ":" + typeName(parameter->type)},
                {"documentation", {{"kind", "markdown"}, {"value", parameter->doc}}}
            });
            if (index >= argumentTypes.size() || argumentTypes[index] == nullptr || parameter->type == nullptr) continue;
            if (parameter->type != argumentTypes[index]) {
                if (!parameter->type->acceptsArgument(argumentTypes[index])) compatible = false;
                ++score;
            }
        }
        if (compatible && score < bestScore) {
            selected = signatures.size();
            bestScore = score;
        }
        auto documentation = callable->getDoc();
        if (const auto* script = callable->asScriptCallable()) {
            documentation = "Defined in `" + script->getPath() + "`.";
        }
        json::object signature{
            {"label", callableLabel(runtime, *callable)},
            {"documentation", {{"kind", "markdown"}, {"value", documentation}}},
            {"parameters", std::move(parameterInformation)}
        };
        if (!parameters.empty()) signature["activeParameter"] = std::min(activeParameter, parameters.size() - 1);
        signatures.push_back(std::move(signature));
    }
    json::object result{{"signatures", std::move(signatures)}, {"activeSignature", selected}};
    const auto selectedParameters = visibleParameters(runtime, *callables[selected]);
    if (!selectedParameters.empty()) result["activeParameter"] = std::min(activeParameter, selectedParameters.size() - 1);
    return result;
}

} // namespace

json::value createSignatureHelp(
    Runtime& runtime,
    const NomadString& sourceName,
    const NomadIndex line,
    const NomadString& linePrefix,
    const std::function<const Type*(const NomadString&)>& findValueType
) {
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get(), CompilerMode::Interpreter);
    if (const auto* callable = runtime.getScriptCallableAtLine(sourceName, line)) {
        context.setHub(callable->getHub());
    }
    Tokenizer source(&context, sourceName, linePrefix);
    if (!source.nextLine() || context.hasError()) return nullptr;
    for (const auto* declaration : {"fun", "init", "class", "open", "hub", "enum", "const", "end"}) {
        if (source.is(declaration)) return nullptr;
    }
    const auto* receiver = findValueType("this");
    std::unique_ptr<ScriptCallableBase> scope;
    if (receiver != nullptr) {
        scope = std::make_unique<ScriptMethod>("$signature", receiver, std::vector<Parameter>{},
            runtime.getVoidType(), sourceName, "");
    } else {
        scope = std::make_unique<ScriptFunction>(CallableId{}, "$signature", sourceName, "");
    }
    scope->setHub(context.getHub());
    for (auto index = NomadIndex{0}; index < source.getTokenCount(); ++index) {
        const auto& name = source.getTokenAt(index);
        if (source.getTokenTypeAt(index) == TokenType::Identifier && name != "this" &&
            name.find('.') == NomadString::npos) {
            if (const auto* type = findValueType(name)) scope->registerVariable(name, type);
        }
    }
    std::vector<std::pair<NomadIndex, std::vector<const Callable*>>> callSites;
    for (auto index = NomadIndex{0}; index < source.getTokenCount(); ++index) {
        if (source.getTokenAt(index) == "fun" || source.getTokenAt(index) == "then") return nullptr;
        if (source.getTokenTypeAt(index) != TokenType::Identifier) continue;
        context.clearDiagnostics();
        Tokenizer tokens(&context, sourceName, linePrefix);
        if (!tokens.nextLine()) continue;
        for (auto skipped = NomadIndex{0}; skipped < index; ++skipped) tokens.next();
        auto callables = findCallables(runtime, context, tokens, findValueType);
        if (callables.empty()) {
            const auto& name = source.getTokenAt(index);
            if (findValueType(name) == nullptr && runtime.getTypeByName(name) == nullptr &&
                parser::isNativeMemberCandidate(compiler.get(), scope.get(), name)) return nullptr;
            continue;
        }
        // These shapes can create script bodies or consume event-specific arguments.
        // Reject them before parsing even an enclosing expression.
        for (const auto* callable : callables) {
            for (const auto* parameter : visibleParameters(runtime, *callable)) {
                if (runtime.getParameterShape(parameter->type) != ParameterShape::Value) return nullptr;
            }
        }
        callSites.emplace_back(tokens.getColumnIndex(), std::move(callables));
    }
    json::value result;
    const auto trailingSpace = !linePrefix.empty() && (linePrefix.back() == ' ' || linePrefix.back() == '\t');
    for (const auto& [argumentColumn, callables] : callSites) {
        context.clearDiagnostics();
        Tokenizer tokens(&context, sourceName, linePrefix);
        if (!tokens.nextLine()) continue;
        while (!tokens.endOfLine() && tokens.getColumnIndex() != argumentColumn) tokens.next();
        std::vector<const Parameter*> shape;
        for (const auto* callable : callables) {
            auto parameters = visibleParameters(runtime, *callable);
            if (parameters.size() > shape.size()) shape = std::move(parameters);
        }
        std::vector<const Type*> argumentTypes;
        auto activeParameter = std::size_t{0};
        while (!tokens.endOfLine() && activeParameter < shape.size()) {
            if (tokens.is(")") || tokens.is("]")) break;
            const auto* parameter = shape[activeParameter];
            ArgumentList arguments;
            if (!parser::parseCallableArguments(&context, scope.get(), &tokens,
                std::vector<Parameter>{*parameter}, &arguments)) break;
            const auto& argument = arguments.getArguments().front();
            argumentTypes.push_back(argument->resolveOverloadType(&context, scope.get()));
            context.clearDiagnostics();
            if (!tokens.endOfLine() || trailingSpace) ++activeParameter;
        }
        // A closing delimiter or remaining tokens put the cursor outside this call.
        if (!tokens.endOfLine()) continue;
        if (!result.is_null() && activeParameter >= shape.size() && trailingSpace) continue;
        result = describeSignatures(runtime, callables, argumentTypes, activeParameter);
    }
    return result;
}

} // namespace nomad
