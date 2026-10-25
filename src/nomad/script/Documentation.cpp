// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/script/Documentation.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/Parser.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Type.hpp>

#include <boost/json.hpp>

#include <algorithm>
#include <set>

namespace nomad {

namespace {

struct NativeMemberDocumentationInfo {
    MemberReference id;
    MemberType memberType;
    const Type* objectType;
    NomadString name;
};

std::vector<NativeMemberDocumentationInfo> getNativeMemberDocumentation(const Runtime* runtime) {
    std::vector<MemberReference> memberIds;
    runtime->getNativeMembers(memberIds);

    std::vector<NativeMemberDocumentationInfo> members;
    members.reserve(memberIds.size());

    for (const auto memberId : memberIds) {
        switch (runtime->getMemberType(memberId)) {
        case MemberType::Property: {
            if (const auto info = runtime->getNativePropertyInfo(memberId)) {
                members.emplace_back(memberId, MemberType::Property, info->objectType, info->name);
            }
            break;
        }

        case MemberType::Method: {
            const auto* method = runtime->getNativeMemberMethod(memberId);
            if (method != nullptr) {
                members.emplace_back(memberId, MemberType::Method, method->getObjectType(), method->getName());
            }
            break;
        }

        case MemberType::Unknown:
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

std::vector<NomadString> getNativeMemberNames(const Runtime* runtime) {
    std::vector<NomadString> names;

    for (const auto& member : getNativeMemberDocumentation(runtime)) {
        if (std::ranges::find(names, member.name) == names.end()) {
            names.push_back(member.name);
        }
    }

    std::ranges::sort(names);
    return names;
}

NomadString getDocumentedTypeName(const Runtime*, const Type* type) {
    return type->getTypeName();
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
    out << "# Nomad Engine Reference" << std::endl;
    out << std::endl;
    out << "This reference describes registered script APIs."
        << std::endl;
    out << std::endl;

    out << "## Constants" << std::endl;

    struct constantInfo {
        NomadString name;
        const Type* type;
        RuntimeValue value;
    };

    std::vector<constantInfo> constants;
    std::vector<const EnumType*> enums;

    auto constantId = runtime->getFirstConstantId();

    while (constantId.isValid()) {
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
            constantTextValue = runtime->getScriptFunction(CallableId{constant.value.getIdValue()})->getName();
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

    while (variableContextId.isValid()) {
        auto context = runtime->getVariableContext(variableContextId);

        auto variableId = context->getFirstVariableId();

        while (variableId.isValid()) {
            auto name = context->getVariableName(variableId);
            auto type = context->getVariableType(variableId);

            variables.emplace_back(name, type, VariableType::Context);

            variableId = context->getNextVariableId(variableId);
        }

        variableContextId = runtime->getNextVariableContextId(variableContextId);
    }

    auto dynamicVariableId = runtime->getFirstDynamicVariableId();

    while (dynamicVariableId.isValid()) {
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

    generateTypeDocumentation(runtime, out);

    generateNativeMemberDocumentation(runtime, out);

    out << "## Functions" << std::endl;

    enum class FunctionType {
        Native,
        Script,
    };

    struct FunctionInfo {
        NomadString name;
        FunctionType functionType;
        NomadString doc;
        std::vector<Parameter> parameters;
        const Type* returnType = nullptr;
    };

    std::vector<FunctionInfo> functions;

    auto nativeFunctionId = runtime->getFirstNativeFunctionId();

    while (nativeFunctionId.isValid()) {
        const auto* nativeFunction = runtime->getNativeFunction(nativeFunctionId);
        if (nativeFunction != nullptr) {
            functions.emplace_back(
                nativeFunction->getName(),
                FunctionType::Native,
                nativeFunction->getDoc(),
                nativeFunction->getParameters(),
                nativeFunction->getReturnType()
            );
        }

        nativeFunctionId = runtime->getNextNativeFunctionId(nativeFunctionId);
    }

    std::vector<ScriptFunction*> scriptFunctions;

    runtime->getFunctions(scriptFunctions);

    for (auto function: scriptFunctions) {
        std::vector<Parameter> parameters;

        for (NomadIndex i = 0; i < function->getParameterCount(); i++) {
            auto name = function->getParameterName(toNomadId(i));
            auto type = function->getParameterType(toNomadId(i));

            parameters.emplace_back(
                name,
                type,
                ""
            );
        }

        functions.emplace_back(
            function->getName(),
            FunctionType::Script,
            "",
            parameters,
            function->getReturnType()
        );
    }

    std::ranges::sort(functions, [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    for (const auto& function: functions) {
        out << "### `" << function.name << "`";

        const auto functionTypeText =
            function.functionType == FunctionType::Native ? "Native" : "Script";

        out << " (*" << functionTypeText << "*)" << std::endl;

        out << std::endl;

        if (function.parameters.empty()) {
            out << "No parameters." << std::endl;
            out << std::endl;
        } else {
            out << "parameters:" << std::endl;
            out << std::endl;

            for (const auto& parameter: function.parameters) {
                const auto parameterTypeName = parameter.type->getTypeName();

                out << "- `" << parameter.name << ":" << parameterTypeName << "`";

                if (parameter.doc.empty() == false) {
                    out << " - " << parameter.doc;
                }

                out << std::endl;
            }
        }

        out << std::endl;

        const auto returnTypeName = function.returnType
            ? function.returnType->getTypeName()
            : runtime->getVoidType()->getTypeName();

        out << "`returns " << returnTypeName << "`" << std::endl;

        out << std::endl;

        if (function.doc.empty() == false) {
            out << function.doc << std::endl;
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

void generateTypeDocumentation(const Runtime* runtime, std::ostream& out) {
    std::vector<const Type*> types;
    runtime->getTypes(types);

    std::erase_if(types, [](const Type* type) {
        return type->isInternal() || type->getDocumentation().empty();
    });
    std::ranges::sort(types, {}, &Type::getTypeName);

    if (types.empty()) {
        return;
    }

    out << "## Types" << std::endl;

    for (const auto* type : types) {
        out << std::endl;
        out << "### `" << type->getTypeName() << "`" << std::endl;
        out << std::endl;
        out << type->getDocumentation() << std::endl;
        for (const auto* candidate : runtime->getConstructors()) {
            const auto& constructor = *candidate;
            if (constructor.getReturnType() != type) {
                continue;
            }
            out << std::endl << "Constructor: `new " << constructor.getName();
            for (auto index = NomadIndex{0}; index < constructor.getParameters().size(); ++index) {
                out << " " << constructor.getParameters()[index].name << ":"
                    << getDocumentedTypeName(runtime, constructor.getParameters()[index].type);
            }
            out << "` -> `" << type->getTypeName() << "`" << std::endl;
        }
    }

    out << std::endl;
}

void generateNativeMemberDocumentation(const Runtime* runtime, std::ostream& out) {
    out << "## Object Members" << std::endl;
    out << std::endl;
    out << "Properties and methods are available on instances of each object type." << std::endl;

    const Type* currentType = nullptr;

    for (const auto& member : getNativeMemberDocumentation(runtime)) {
        if (member.objectType != currentType) {
            currentType = member.objectType;
            out << std::endl;
            out << "### `" << currentType->getTypeName() << "`" << std::endl;
            out << std::endl;
        }

        out << "#### `" << member.name << "`";

        if (member.memberType == MemberType::Property) {
            const auto info = runtime->getNativePropertyInfo(member.id);
            if (!info) {
                throw NomadException("Registered native property is unavailable");
            }

            NomadString access;
            if (info->getFn && info->setFn) {
                access = "read/write";
            } else if (info->getFn) {
                access = "read-only";
            } else {
                access = "write-only";
            }

            out << " (*" << access << " property*)" << std::endl;
            out << std::endl;
            out << "`" << member.name << ":" << getDocumentedTypeName(runtime, info->valueType) << "`"
                << std::endl;
            out << std::endl;
            if (!info->doc.empty()) {
                out << info->doc << std::endl;
                out << std::endl;
            }
        } else {
            const auto* method = runtime->getNativeMemberMethod(member.id);
            if (method == nullptr) {
                continue;
            }

            out << " (*method*)" << std::endl;
            out << std::endl;

            if (method->getParameters().empty()) {
                out << "No parameters." << std::endl;
            } else {
                out << "parameters:" << std::endl;
                out << std::endl;

                for (const auto& parameter : method->getParameters()) {
                    out << "- `" << parameter.name << ":" << getDocumentedTypeName(runtime, parameter.type) << "`";
                    if (!parameter.doc.empty()) {
                        out << " - " << parameter.doc;
                    }
                    out << std::endl;
                }
            }

            out << std::endl;
            out << "`returns " << getDocumentedTypeName(runtime, method->getReturnType()) << "`" << std::endl;
            out << std::endl;
            if (!method->getDoc().empty()) {
                out << method->getDoc() << std::endl;
                out << std::endl;
            }
        }

        out << "---" << std::endl;
    }

    for (const auto* info : runtime->getNativeIndexers()) {
        const auto& indexer = *info;
        out << std::endl;
        out << "### `" << indexer.objectType->getTypeName() << "[";
        for (auto index = NomadIndex{0}; index < indexer.indexTypes.size(); ++index) {
            if (index != 0) {
                out << " ";
            }
            out << "index";
            if (indexer.indexTypes.size() != 1) {
                out << index + 1;
            }
        }
        out << "]`" << std::endl;
        const auto access = indexer.getFn ? (indexer.setFn ? "read/write" : "read-only") : "write-only";
        out << std::endl << "*" << access << " indexer*" << std::endl;
        out << std::endl;
        for (auto index = NomadIndex{0}; index < indexer.indexTypes.size(); ++index) {
            out << "- Index";
            if (indexer.indexTypes.size() != 1) {
                out << " " << index + 1;
            }
            out << ": `" << getDocumentedTypeName(runtime, indexer.indexTypes[index]) << "`" << std::endl;
        }
        out << "- Value: `" << getDocumentedTypeName(runtime, indexer.valueType) << "`" << std::endl;
        if (!indexer.doc.empty()) {
            out << std::endl << indexer.doc << std::endl;
        }
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

    const auto nativeFunctions = runtime->getNativeFunctions();

    for (const auto* nativeFunction : nativeFunctions) {
        out << nativeFunction->getName() << "\n";
    }

    out << "--- NativeMembers ---\n";

    for (const auto& name : getNativeMemberNames(runtime)) {
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
        "classes",
        "blocks",
        "keywords",
        "literals",
        "functions",
        "nativeMembers",
        "types",
        "operators"
    }) {
        rootPatterns.push_back(createInclude(repositoryName));
    }
    root["patterns"] = std::move(rootPatterns);

    object repository;
    repository["comments"] = createPatternRepositoryEntry(
        createMatchPattern("comment.line.number-sign.nomad", "#.*$")
    );
    repository["operators"] = createPatternRepositoryEntry(
        createMatchPattern("punctuation.operator.nomad", "[+\\-*/%=&|<>!^$]+")
    );

    object formatString;
    formatString["name"] = "string.quoted.double.format.nomad";
    formatString["begin"] = "\\$\"";
    formatString["beginCaptures"] = object({{"0", object({{"name", "punctuation.definition.string.begin.nomad"}})}});
    formatString["end"] = "\"";
    formatString["endCaptures"] = object({{"0", object({{"name", "punctuation.definition.string.end.nomad"}})}});

    array interpolationPatterns;
    for (const auto& repositoryName : {
        "formatStrings", "strings", "keywords", "numbers", "functions", "nativeMembers", "literals",
        "types", "operators"
    }) {
        interpolationPatterns.push_back(createInclude(repositoryName));
    }

    object interpolation;
    interpolation["name"] = "meta.interpolation.nomad";
    interpolation["begin"] = "\\{";
    interpolation["beginCaptures"] = object({{"0", object({{"name", "punctuation.section.interpolation.begin.nomad"}})}});
    interpolation["end"] = "\\}";
    interpolation["endCaptures"] = object({{"0", object({{"name", "punctuation.section.interpolation.end.nomad"}})}});
    interpolation["patterns"] = std::move(interpolationPatterns);

    array formatStringPatterns;
    formatStringPatterns.push_back(
        createMatchPattern("constant.character.escape.nomad", R"(\\["\\nrt])")
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

    const auto ordinaryPatterns = array({
        createInclude("comments"), createInclude("formatStrings"), createInclude("strings"),
        createInclude("numbers"), createInclude("blocks"), createInclude("keywords"),
        createInclude("literals"), createInclude("functions"), createInclude("nativeMembers"),
        createInclude("types"), createInclude("operators")
    });
    object block;
    block["begin"] = "^\\s*(?:if\\b(?![^#\\n]*\\b(?:break|continue)\\b)|while\\b|until\\b|do\\b|for\\b)|\\bfun\\b";
    block["beginCaptures"] = object({{"0", object({{"name", "keyword.control.nomad"}})}});
    block["end"] = "^\\s*end\\b";
    block["patterns"] = ordinaryPatterns;
    repository["blocks"] = createPatternRepositoryEntry(std::move(block));

    object constructor;
    constructor["begin"] = "^\\s*(init)\\b";
    constructor["beginCaptures"] = object({{"1", object({{"name", "keyword.control.nomad"}})}});
    constructor["end"] = "^\\s*end\\b";
    constructor["patterns"] = ordinaryPatterns;
    repository["constructors"] = createPatternRepositoryEntry(std::move(constructor));

    object classDeclaration;
    classDeclaration["begin"] = "^\\s*(?:(open)\\s+)?(class)\\s+([A-Za-z_][A-Za-z0-9_.]*)\\s*(?:#.*)?$";
    classDeclaration["beginCaptures"] = object({
        {"1", object({{"name", "keyword.control.nomad"}})},
        {"2", object({{"name", "keyword.control.nomad"}})},
        {"3", object({{"name", "entity.name.type.nomad"}})}
    });
    classDeclaration["end"] = "^\\s*end\\b";
    auto classPatterns = array({createInclude("constructors")});
    for (const auto& pattern : ordinaryPatterns) classPatterns.push_back(pattern);
    classDeclaration["patterns"] = std::move(classPatterns);
    repository["classes"] = createPatternRepositoryEntry(std::move(classDeclaration));

    repository["literals"] = createPatternRepositoryEntry(
        createMatchPattern("constant.language.boolean.nomad", createWordPattern({"true", "false"}))
    );

    std::vector<NomadString> nativeFunctionNames;
    const auto nativeFunctions = runtime->getNativeFunctions();
    nativeFunctionNames.reserve(nativeFunctions.size());
    for (const auto* nativeFunction : nativeFunctions) {
        nativeFunctionNames.push_back(nativeFunction->getName());
    }
    repository["functions"] = createPatternRepositoryEntry(
        createMatchPattern("support.function.nomad", createWordPattern(nativeFunctionNames))
    );

    repository["nativeMembers"] = createPatternRepositoryEntry(
        createMatchPattern(
            "support.function.member.nomad",
            "(?<=\\.)" + createWordPattern(getNativeMemberNames(runtime))
        )
    );

    std::vector<const Type*> types;
    runtime->getTypes(types);

    std::vector<NomadString> typeNames;
    typeNames.reserve(types.size());
    for (const auto* type : types) {
        if (!type->isInternal()) {
            typeNames.push_back(type->getTypeName());
        }
    }

    repository["types"] = createPatternRepositoryEntry(
        createMatchPattern("storage.type.nomad", createWordPattern(typeNames))
    );

    root["repository"] = std::move(repository);
    root["foldingStartMarker"] = "\\b(class|fun|hub|if|else|enum)\\b|^\\s*init\\b";
    root["foldingStopMarker"] = "\\bend\\b";
    out << boost::json::serialize(root) << std::endl;
}

} // namespace nomad
