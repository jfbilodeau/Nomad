// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Documentation.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/Parser.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <boost/json.hpp>

#include <set>

namespace nomad {

namespace {

struct IntrinsicMemberDocumentationInfo {
    NomadId id;
    IntrinsicMemberType memberType;
    const Type* objectType;
    NomadString name;
};

std::vector<IntrinsicMemberDocumentationInfo> getIntrinsicMemberDocumentation(const Runtime* runtime) {
    std::vector<NomadId> memberIds;
    runtime->getIntrinsicMembers(memberIds);

    std::vector<IntrinsicMemberDocumentationInfo> members;
    members.reserve(memberIds.size());

    for (const auto memberId : memberIds) {
        switch (runtime->getIntrinsicMemberType(memberId)) {
        case IntrinsicMemberType::Property: {
            IntrinsicPropertyDefinition definition;
            if (runtime->getIntrinsicPropertyDefinition(memberId, definition)) {
                members.emplace_back(memberId, IntrinsicMemberType::Property, definition.objectType, definition.name);
            }
            break;
        }

        case IntrinsicMemberType::Method: {
            IntrinsicMethodDefinition definition;
            if (runtime->getIntrinsicMethodDefinition(memberId, definition)) {
                members.emplace_back(memberId, IntrinsicMemberType::Method, definition.objectType, definition.name);
            }
            break;
        }

        case IntrinsicMemberType::Unknown:
            break;
        }
    }

    std::ranges::sort(members, [](const auto& left, const auto& right) {
        const auto leftType = left.objectType->getTypeName();
        const auto rightType = right.objectType->getTypeName();
        return leftType == rightType ? left.name < right.name : leftType < rightType;
    });

    return members;
}

std::vector<NomadString> getIntrinsicMemberNames(const Runtime* runtime) {
    std::vector<NomadString> names;

    for (const auto& member : getIntrinsicMemberDocumentation(runtime)) {
        if (std::ranges::find(names, member.name) == names.end()) {
            names.push_back(member.name);
        }
    }

    std::ranges::sort(names);
    return names;
}

NomadString getDocumentedTypeName(const Runtime* runtime, const Type* type) {
    return type == runtime->getStringRefType() ? runtime->getStringType()->getTypeName() : type->getTypeName();
}

NomadString escapeRegularExpression(const NomadString& text) {
    constexpr std::string_view specialCharacters = R"(\.^$|()[]{}*+?)";
    NomadString escaped;
    escaped.reserve(text.size() * 2);

    for (const auto character : text) {
        if (specialCharacters.find(character) != std::string_view::npos) {
            escaped += '\\';
        }
        escaped += character;
    }

    return escaped;
}

NomadString createWordPattern(const std::vector<NomadString>& words) {
    std::set<NomadString> uniqueWords(words.begin(), words.end());
    NomadString pattern = "\\b(?:";
    auto separator = "";

    for (const auto& word : uniqueWords) {
        pattern += separator;
        pattern += escapeRegularExpression(word);
        separator = "|";
    }

    pattern += ")\\b";
    return pattern;
}

boost::json::object createInclude(const NomadString& repositoryName) {
    boost::json::object include;
    include["include"] = "#" + repositoryName;
    return include;
}

boost::json::object createMatchPattern(const NomadString& scopeName, const NomadString& match) {
    boost::json::object pattern;
    pattern["name"] = scopeName;
    pattern["match"] = match;
    return pattern;
}

boost::json::object createPatternRepositoryEntry(boost::json::object pattern) {
    boost::json::array patterns;
    patterns.push_back(std::move(pattern));

    boost::json::object entry;
    entry["patterns"] = std::move(patterns);
    return entry;
}

} // namespace

void generateDocumentation(const Runtime* runtime, std::ostream& out) {
    // Output TOC
    out << "# Nomad Engine Reference" << std::endl;

    out << "* [Constants](#constants)" << std::endl;
    out << "* [Enums](#enums)" << std::endl;
    out << "* [Variables](#variables)" << std::endl;
    out << "* [Intrinsic Members](#intrinsic-members)" << std::endl;
    out << "* [Native Functions](#native-functions)" << std::endl;

    out << "---" << std::endl;

    out << "## Constants" << std::endl;

    struct constantInfo {
        NomadString name;
        const Type* type;
        RuntimeValue value;
    };

    std::vector<constantInfo> constants;
    std::vector<const EnumType*> enums;

    NomadId constantId = runtime->getFirstConstantId();

    while (constantId != NOMAD_INVALID_ID) {
        auto name = runtime->getConstantName(constantId);
        auto type = runtime->getConstantType(constantId);
        RuntimeValue value;
        runtime->getConstantValue(constantId, value);

        if (const auto* enumType = type->asEnum()) {
            if (std::ranges::find(enums, enumType) == enums.end()) {
                enums.push_back(enumType);
            }
        } else {
            constants.emplace_back(
                name,
                type,
                value
            );
        }

        constantId = runtime->getNextConstantId(constantId);
    }

    std::ranges::sort(constants, [](const constantInfo& a, const constantInfo& b) {
        return a.name < b.name;
    });

    for (const auto& constant : constants) {
        NomadString constantTextValue;

        if (constant.type == runtime->getVoidType()) {
            constantTextValue = "";
        } else if (constant.type == runtime->getBooleanType()) {
            constantTextValue = constant.value.getBooleanValue() ? "true" : "false";
        } else if (constant.type == runtime->getIntegerType()) {
            constantTextValue = std::format("{0} ({0:#0x})", constant.value.getIntegerValue());
        } else if (constant.type == runtime->getFloatType()) {
            constantTextValue = std::format("{:}", constant.value.getFloatValue());
        } else if (constant.type == runtime->getStringType()) {
            NomadString text = constant.value.getStringValue();
            NomadString escaped;

            escaped.reserve(text.size());

            for (char ch : text) {
                switch (ch) {
                    case '\\': escaped += "\\\\"; break;
                    case '\n': escaped += "\\n"; break;
                    case '\t': escaped += "\\t"; break;
                    case '\r': escaped += "\\r"; break;
                    case '\"': escaped += "\\\""; break;
                    case '\b': escaped += "\\b"; break;
                    case '\f': escaped += "\\f"; break;
                    default: {
                        escaped += ch;
                    }
                }
            }
            constantTextValue = std::format("\"{}\"", escaped);
        } else if (constant.type == runtime->getFunctionType()) {
            constantTextValue = runtime->getFunction(constant.value.getIdValue())->getName();
        } else {
            constantTextValue = "<unknown_type>";
        }

        out
            << "* `"
            << constant.name
            << ":"
            << constant.type->getTypeName()
            << " = "
            << constantTextValue
            << "`"
            << std::endl;
    }

    out << std::endl;

    std::ranges::sort(enums, {}, [](const EnumType* type) {
        return type->getTypeName();
    });

    out << "## Enums" << std::endl;

    for (const auto* enumType: enums) {
        out << std::endl;
        out << "### " << enumType->getTypeName() << std::endl;
        out << std::endl;

        for (NomadIndex index = 0; index < enumType->getValueCount(); ++index) {
            out << "* `" << enumType->getTypeName() << "." << enumType->getValue(index).name << "`" << std::endl;
        }
    }

    out << std::endl;

    enum class VariableType {
        Context,
        Dynamic,
    };

    struct VariableInfo {
        NomadString name;
        const Type* type;
        VariableType variableType;
    };

    std::vector<VariableInfo> variables;

    auto variableContextId = runtime->getFirstVariableContextId();

    while (variableContextId != NOMAD_INVALID_ID) {
        auto context = runtime->getVariableContext(variableContextId);

        auto variableId = context->getFirstVariableId();

        while (variableId != NOMAD_INVALID_ID) {
            auto name = context->getVariableName(variableId);
            auto type = context->getVariableType(variableId);

            variables.emplace_back(name, type, VariableType::Context);

            variableId = context->getNextVariableId(variableId);
        }

        variableContextId = runtime->getNextVariableContextId(variableContextId);
    }

    auto dynamicVariableId = runtime->getFirstDynamicVariableId();

    while (dynamicVariableId != NOMAD_INVALID_ID) {
        auto name = runtime->getDynamicVariableName(dynamicVariableId);
        auto type = runtime->getDynamicVariableType(dynamicVariableId);

        variables.emplace_back(name, type, VariableType::Dynamic);

        dynamicVariableId = runtime->getNextDynamicVariableId(dynamicVariableId);
    }

    std::ranges::sort(variables, [](const VariableInfo& a, const VariableInfo& b) {
        return a.name < b.name;
    });

    out << "## Variables" << std::endl;

    for (auto& variable: variables) {
        const NomadString variableTypeText = (variable.variableType == VariableType::Context) ? "context" : "dynamic";

        out << "- `" << variable.name << ":" << variable.type->getTypeName() << "` (" << variableTypeText << ")" << std::endl;
    }

    out << std::endl;

    generateIntrinsicMemberDocumentation(runtime, out);

    out << "## Native Functions" << std::endl;

    enum class NativeFunctionType {
        NativeFunction,
        Function,
    };

    struct NativeFunctionInfo {
        NomadString name;
        NativeFunctionType nativeFunctionType;
        NomadString doc;
        std::vector<NativeFunctionParameterDefinition> parameters;
        const Type* returnType = nullptr;
    };

    std::vector<NativeFunctionInfo> nativeFunctions;

    auto nativeFunctionId = runtime->getFirstNativeFunctionId();

    while (nativeFunctionId != NOMAD_INVALID_ID) {
        NativeFunctionDefinition nativeFunctionDefinition;

        runtime->getNativeFunctionDefinition(nativeFunctionId, nativeFunctionDefinition);

        nativeFunctions.emplace_back(
            nativeFunctionDefinition.name,
            NativeFunctionType::NativeFunction,
            nativeFunctionDefinition.doc,
            nativeFunctionDefinition.parameters,
            nativeFunctionDefinition.returnType
        );

        nativeFunctionId = runtime->getNextNativeFunctionId(nativeFunctionId);
    }

    std::vector<Function*> functions;

    runtime->getFunctions(functions);

    for (auto function: functions) {
        std::vector<NativeFunctionParameterDefinition> parameters;

        for (NomadIndex i = 0; i < function->getParameterCount(); i++) {
            auto name = function->getParameterName(toNomadId(i));
            auto type = function->getParameterType(toNomadId(i));

            parameters.emplace_back(
                name,
                type,
                ""
            );
        }

        nativeFunctions.emplace_back(
            function->getName(),
            NativeFunctionType::Function,
            "",
            parameters,
            function->getReturnType()
        );
    }

    std::ranges::sort(nativeFunctions, [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    for (auto& nativeFunction: nativeFunctions) {
        out << "### `" << nativeFunction.name << "`";

        auto nativeFunctionTypeText = (nativeFunction.nativeFunctionType == NativeFunctionType::NativeFunction) ? "Native" : "Script";

        out << " (*" << nativeFunctionTypeText << "*)" << std::endl;

        out << std::endl;

        if (nativeFunction.parameters.empty()) {
            out << "No parameters." << std::endl;
            out << std::endl;
        } else {
            out << "parameters:" << std::endl;

            for (const auto& parameter: nativeFunction.parameters) {
                // Callers pass a regular string to a `$stringref` parameter.
                const auto parameterTypeName = parameter.type == runtime->getStringRefType()
                    ? runtime->getStringType()->getTypeName()
                    : parameter.type->getTypeName();

                out << "- `" << parameter.name << ":" << parameterTypeName << "`";

                if (parameter.doc.empty() == false) {
                    out << " - " << parameter.doc;
                }

                out << std::endl;
            }
        }

        out << std::endl;

        auto returnTypeName = nativeFunction.returnType ? nativeFunction.returnType->getTypeName() : runtime->getVoidType()->getTypeName();

        out << "`returns " << returnTypeName << "`" << std::endl;

        out << std::endl;

        if (nativeFunction.doc.empty() == false) {
            out << nativeFunction.doc << std::endl;
            out << std::endl;
        }
        out << "---" << std::endl;
    }

    out << std::endl;
    //
    // out << "## Instructions" << std::endl;
    //
    // for (auto& opCode: runtime->getInstructions()) {
    //     out << "`" << opCode.name << "`" << std::endl;
    //     out << opCode.doc << std::endl;
    //     out << std::endl;
    // }

    out.flush();
}

void generateIntrinsicMemberDocumentation(const Runtime* runtime, std::ostream& out) {
    out << "## Intrinsic Members" << std::endl;
    out << std::endl;
    out << "Intrinsic properties and methods are available on values of the receiver type." << std::endl;

    const Type* currentType = nullptr;

    for (const auto& member : getIntrinsicMemberDocumentation(runtime)) {
        if (member.objectType != currentType) {
            currentType = member.objectType;
            out << std::endl;
            out << "### `" << currentType->getTypeName() << "`" << std::endl;
            out << std::endl;
        }

        out << "#### `" << member.name << "`";

        if (member.memberType == IntrinsicMemberType::Property) {
            IntrinsicPropertyDefinition definition;
            (void)runtime->getIntrinsicPropertyDefinition(member.id, definition);

            NomadString access;
            if (definition.getFn && definition.setFn) {
                access = "read/write";
            } else if (definition.getFn) {
                access = "read-only";
            } else {
                access = "write-only";
            }

            out << " (*" << access << " property*)" << std::endl;
            out << std::endl;
            out << "`" << member.name << ":" << getDocumentedTypeName(runtime, definition.valueType) << "`"
                << std::endl;
            out << std::endl;
            if (!definition.doc.empty()) {
                out << definition.doc << std::endl;
                out << std::endl;
            }
        } else {
            IntrinsicMethodDefinition definition;
            (void)runtime->getIntrinsicMethodDefinition(member.id, definition);

            out << " (*" << (definition.mutatesReceiver ? "mutating " : "") << "method*)" << std::endl;
            out << std::endl;

            if (definition.parameters.empty()) {
                out << "No parameters." << std::endl;
            } else {
                out << "parameters:" << std::endl;
                for (const auto& parameter : definition.parameters) {
                    out << "- `" << parameter.name << ":" << getDocumentedTypeName(runtime, parameter.type) << "`";
                    if (!parameter.doc.empty()) {
                        out << " - " << parameter.doc;
                    }
                    out << std::endl;
                }
            }

            out << std::endl;
            out << "`returns " << getDocumentedTypeName(runtime, definition.returnType) << "`" << std::endl;
            out << std::endl;
            if (!definition.doc.empty()) {
                out << definition.doc << std::endl;
                out << std::endl;
            }
        }

        out << "---" << std::endl;
    }

    out << std::endl;
}

void generateKeywords(Runtime* runtime, std::ostream& out) {
    out << "--- Keywords ---\n";

    std::vector<KeywordDefinition> keywords;

    runtime->getKeywords(keywords);

    for (const KeywordDefinition& keyword : keywords) {
        out << keyword.keyword << "\n";
    }

    out << "--- Statements --- \n";

    auto compiler = runtime->createCompiler();
    std::vector<NomadString> statements;
    compiler->getRegisteredStatements(statements);

    for (auto& statement : statements) {
        out << statement << "\n";
    }

    out << "--- Native Functions ---\n";

    std::vector<NativeFunctionDefinition> nativeFunctions;
    runtime->getNativeFunctions(nativeFunctions);

    for (const NativeFunctionDefinition& nativeFunction : nativeFunctions) {
        out << nativeFunction.name << "\n";
    }

    out << "--- IntrinsicMembers ---\n";

    for (const auto& name : getIntrinsicMemberNames(runtime)) {
        out << name << "\n";
    }
}

void generateTextMateGrammar(Runtime* runtime, std::ostream& out) {
    using boost::json::array;
    using boost::json::object;

    object root;
    root["$schema"] = "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json";
    root["name"] = "Nomad";
    root["scopeName"] = "source.nomad";
    root["fileTypes"] = array({"nomad"});

    array rootPatterns;
    for (const auto& repositoryName : {
        "comments",
        "formatStrings",
        "strings",
        "numbers",
        "keywords",
        "literals",
        "functions",
        "intrinsicMembers",
        "types"
    }) {
        rootPatterns.push_back(createInclude(repositoryName));
    }
    rootPatterns.push_back(createMatchPattern("punctuation.operator.nomad", "[+\\-*/%=&|<>!]+"));
    root["patterns"] = std::move(rootPatterns);

    object repository;
    repository["comments"] = createPatternRepositoryEntry(
        createMatchPattern("comment.line.number-sign.nomad", "#.*$")
    );

    object formatString;
    formatString["name"] = "string.quoted.double.format.nomad";
    formatString["begin"] = "\\$\"";
    formatString["beginCaptures"] = object({{"0", object({{"name", "punctuation.definition.string.begin.nomad"}})}});
    formatString["end"] = "\"";
    formatString["endCaptures"] = object({{"0", object({{"name", "punctuation.definition.string.end.nomad"}})}});

    array interpolationPatterns;
    for (const auto& repositoryName : {"keywords", "numbers", "functions", "intrinsicMembers", "literals"}) {
        interpolationPatterns.push_back(createInclude(repositoryName));
    }

    object interpolation;
    interpolation["name"] = "meta.interpolation.nomad";
    interpolation["begin"] = "\\{";
    interpolation["end"] = "\\}";
    interpolation["patterns"] = std::move(interpolationPatterns);

    array formatStringPatterns;
    formatStringPatterns.push_back(
        createMatchPattern("constant.character.escape.nomad", "\\\\.")
    );
    formatStringPatterns.push_back(std::move(interpolation));
    formatString["patterns"] = std::move(formatStringPatterns);
    repository["formatStrings"] = createPatternRepositoryEntry(std::move(formatString));

    object string;
    string["name"] = "string.quoted.double.nomad";
    string["begin"] = "\"";
    string["end"] = "\"";
    string["patterns"] = array({
        createMatchPattern("constant.character.escape.nomad", "\\\\.")
    });
    repository["strings"] = createPatternRepositoryEntry(std::move(string));

    object numbers;
    numbers["patterns"] = array({
        createMatchPattern("constant.numeric.float.nomad", "\\b\\d+\\.\\d+\\b"),
        createMatchPattern("constant.numeric.integer.nomad", "\\b\\d+\\b")
    });
    repository["numbers"] = std::move(numbers);

    std::vector<NomadString> controlWords;
    std::vector<KeywordDefinition> keywords;
    runtime->getKeywords(keywords);
    controlWords.reserve(keywords.size());
    for (const auto& keyword : keywords) {
        controlWords.push_back(keyword.keyword);
    }

    auto compiler = runtime->createCompiler();
    std::vector<NomadString> statements;
    compiler->getRegisteredStatements(statements);
    controlWords.insert(controlWords.end(), statements.begin(), statements.end());
    repository["keywords"] = createPatternRepositoryEntry(
        createMatchPattern("keyword.control.nomad", createWordPattern(controlWords))
    );

    repository["literals"] = createPatternRepositoryEntry(
        createMatchPattern("constant.language.boolean.nomad", createWordPattern({"true", "false"}))
    );

    std::vector<NomadString> nativeFunctionNames;
    std::vector<NativeFunctionDefinition> nativeFunctions;
    runtime->getNativeFunctions(nativeFunctions);
    nativeFunctionNames.reserve(nativeFunctions.size());
    for (const auto& nativeFunction : nativeFunctions) {
        nativeFunctionNames.push_back(nativeFunction.name);
    }
    repository["functions"] = createPatternRepositoryEntry(
        createMatchPattern("support.function.nomad", createWordPattern(nativeFunctionNames))
    );

    repository["intrinsicMembers"] = createPatternRepositoryEntry(
        createMatchPattern(
            "support.function.member.nomad",
            "(?<=\\.)" + createWordPattern(getIntrinsicMemberNames(runtime))
        )
    );

    repository["types"] = createPatternRepositoryEntry(
        createMatchPattern(
            "storage.type.nomad",
            createWordPattern({
                runtime->getBooleanType()->getTypeName(),
                runtime->getFloatType()->getTypeName(),
                runtime->getIntegerType()->getTypeName(),
                runtime->getStringType()->getTypeName(),
                runtime->getVoidType()->getTypeName()
            })
        )
    );

    root["repository"] = std::move(repository);
    root["foldingStartMarker"] = "\\b(fun|if|else|enum)\\b";
    root["foldingStopMarker"] = "\\bend\\b";
    out << boost::json::serialize(root) << std::endl;
}

} // namespace nomad
