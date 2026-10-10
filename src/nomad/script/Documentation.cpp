// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/script/Documentation.hpp>
#include <nomad/compiler/Parser.hpp>

#include <nomad/script/Runtime.hpp>

#include <boost/json.hpp>

namespace nomad {

void generateDocumentation(const Runtime* runtime, std::ostream& out) {
    // Output TOC
    out << "# Nomad Engine Reference" << std::endl;

    out << "* [Constants](#constants)" << std::endl;
    out << "* [Variables](#variables)" << std::endl;
    out << "* [NativeFunctions](#nativefunctions)" << std::endl;

    out << "---" << std::endl;

    out << "## Constants" << std::endl;

    struct constantInfo {
        NomadString name;
        const Type* type;
        RuntimeValue value;
    };

    std::vector<constantInfo> constants;

    NomadId constantId = runtime->getFirstConstantId();

    while (constantId != NOMAD_INVALID_ID) {
        auto name = runtime->getConstantName(constantId);
        auto type = runtime->getConstantType(constantId);
        RuntimeValue value;
        runtime->getConstantValue(constantId, value);

        constants.emplace_back(
            name,
            type,
            value
        );

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

    out << "## NativeFunctions" << std::endl;

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

        auto nativeFunctionTypeText = (nativeFunction.nativeFunctionType == NativeFunctionType::NativeFunction) ? "NativeFunction" : "Function";

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

    out << "--- NativeFunctions ---\n";

    std::vector<NativeFunctionDefinition> nativeFunctions;
    runtime->getNativeFunctions(nativeFunctions);

    for (const NativeFunctionDefinition& nativeFunction : nativeFunctions) {
        out << nativeFunction.name << "\n";
    }
}

void generateTextMateGrammar(Runtime* runtime, std::ostream& out) {
    using boost::json::object;
    using boost::json::array;

    object root;

    // File extension and basic syntax
    root["fileExtensions"] = array({".nomad"});

    object comments;
    // Nomad uses '#' for line comments
    comments["line"] = "#";
    root["comments"] = comments;

    root["strings"] = array({"\"", "'"});
    // Format strings: prefix with $ and support { ... } interpolation
    object formatObj;
    formatObj["prefix"] = "$";
    formatObj["delimiter"] = "\"";
    formatObj["interpolationStart"] = "{";
    formatObj["interpolationEnd"] = "}";
    root["formatString"] = formatObj;
    root["numberRegex"] = R"(\b\d+(?:\.\d+)?\b)";
    root["operatorRegex"] = "[+\\-*/%=&|<>!]+";

    // Keywords
    std::vector<KeywordDefinition> keywords;
    runtime->getKeywords(keywords);

    array keywordsArray;

    for (const auto& k : keywords) {
        keywordsArray.push_back(boost::json::value(k.keyword));
    }

    // Statements registered in the compiler
    auto compiler = runtime->createCompiler();
    std::vector<NomadString> statements;
    compiler->getRegisteredStatements(statements);

    array statementsArray;
    for (const auto& s : statements) {
        statementsArray.push_back(boost::json::value(s));
    }

    // NativeFunctions (includes engine nativeFunctions)
    std::vector<NativeFunctionDefinition> nativeFunctions;
    runtime->getNativeFunctions(nativeFunctions);

    array nativeFunctionsArray;
    for (const auto& c : nativeFunctions) {
        // Ensure nativeFunctions are single-line and prefix with '#'
        std::string name = c.name;
        for (char &ch : name) {
            if (ch == '\n' || ch == '\r') ch = ' ';
        }

        nativeFunctionsArray.push_back(boost::json::value(std::string("#") + name));
    }

    root["keywords"] = keywordsArray;
    root["statements"] = statementsArray;
    root["nativeFunctions"] = nativeFunctionsArray;

    // Common literals
    array literalsArray;
    literalsArray.push_back(boost::json::value("true"));
    literalsArray.push_back(boost::json::value("false"));
    root["literals"] = literalsArray;

    // Serialize to output
    out << boost::json::serialize(root) << std::endl;
}

}  // nomad
