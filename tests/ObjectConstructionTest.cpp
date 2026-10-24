// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;

namespace {

class ConstructedObject final : public NomadObject {
};

const ObjectType* registerObjectType(Runtime& runtime, const NomadString& name) {
    auto descriptor = std::make_unique<ObjectType>(
        &runtime, name, ObjectTypeMode::Closed, std::vector<const Type*>{}, "A constructed object."
    );
    const auto* type = descriptor.get();
    runtime.registerType(std::move(descriptor));
    return type;
}

CallableId compileConstruction(Runtime& runtime, const NomadString& source, const NomadString& name = "main") {
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{name, {name + ".nomad", source}}}, {}});
    const auto compiled = compiler->compileFunctions(&context);
    BOOST_REQUIRE_MESSAGE(compiled, (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Compilation failed"));
    return runtime.getFunctionId(name);
}

} // namespace

BOOST_AUTO_TEST_SUITE(object_construction)

BOOST_AUTO_TEST_CASE(inferred_constructors_resolve_execute_and_reuse_specializations) {
    Runtime runtime;
    auto specializations = NomadIndex{0};
    auto constructions = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Box", 1,
        [&](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
            ++specializations;
            return std::make_unique<ObjectType>(owningRuntime, "Box<" + arguments[0]->getTypeName() + ">", ObjectTypeMode::Closed, arguments);
        },
        [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
            const auto* objectType = static_cast<const ObjectType*>(type);
            const auto* elementType = objectType->getTypeArguments()[0];
            BOOST_REQUIRE(runtime.registerNativeConstructor(
                objectType, {defParameter("value", elementType, "")},
                [&](std::span<const RuntimeValue>) {
                    ++constructions;
                    return new ConstructedObject;
                }, ""
            ).isValid());
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Box", {defTemplateParameter("value", TypeParameterIndex{0}, "")}
    ));
    auto caseIndex = NomadIndex{0};
    for (const auto& [source, expected] : std::vector<std::pair<NomadString, NomadString>>{
        {"new Box 42", "Box<int>"},
        {"new Box true", "Box<bool>"},
        {"new Box \"text\"", "Box<string>"},
        {"new Box new Box 42", "Box<Box<int>>"},
        {"new Box<int> 42", "Box<int>"}
    }) {
        BOOST_TEST_CONTEXT(source) {
            const auto id = compileConstruction(runtime, "return " + source, "case" + std::to_string(caseIndex++));
            const auto* expectedType = runtime.getTypeByName(expected);
            BOOST_REQUIRE(expectedType != nullptr);
            BOOST_TEST(runtime.getScriptFunction(id)->getReturnType() == expectedType);
            const auto before = NomadObject::getLiveObjectCount();
            RuntimeValue result;
            runtime.executeFunction(id, {}, result);
            BOOST_REQUIRE(result.getObjectValue() != nullptr);
            expectedType->freeValue(result);
            {
                auto machine = runtime.createVirtualMachine();
                InterpreterContext context(*machine);
                BOOST_REQUIRE(Interpreter{}.execute(source, context));
                BOOST_TEST(context.getResult()->getType() == expectedType);
            }
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        }
    }
    const auto id = compileConstruction(runtime,
        "fun makeNumber\n"
        "    return 42\n"
        "end\n"
        "number = makeNumber\n"
        "return new Box number"
    );
    BOOST_TEST(runtime.getScriptFunction(id)->getReturnType() == runtime.getTypeByName("Box<int>"));
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("number = 42", context));
        BOOST_REQUIRE(Interpreter{}.execute("new Box number", context));
        BOOST_TEST(context.getResult()->getType() == runtime.getTypeByName("Box<int>"));
    }
    BOOST_TEST(specializations == 4u);
    BOOST_TEST(constructions == 13u);
}

BOOST_AUTO_TEST_CASE(inferred_constructors_validate_bindings_and_concrete_constructor_signatures) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Same", 1,
        [](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, "Same<" + arguments[0]->getTypeName() + ">", ObjectTypeMode::Closed, arguments);
        },
        [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
            const auto* objectType = static_cast<const ObjectType*>(type);
            const auto* valueType = objectType->getTypeArguments()[0];
            BOOST_REQUIRE(runtime.registerNativeConstructor(
                objectType, {defParameter("first", valueType, ""), defParameter("second", valueType, "")},
                [](std::span<const RuntimeValue>) { return new ConstructedObject; }, ""
            ).isValid());
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Same",
        {
            defTemplateParameter("first", TypeParameterIndex{0}, ""),
            defTemplateParameter("second", TypeParameterIndex{0}, "")
        }
    ));
    auto caseIndex = NomadIndex{0};
    for (const auto* source : {
        "new Same 1 \"text\"", "new Same 1", "new Same 1 2 3",
        "new Same missing 1", "new Same<void> 1 2"
    }) {
        BOOST_TEST_CONTEXT(source) {
            auto compiler = runtime.createCompiler();
            CompilerContext context(compiler.get());
            const auto name = "rejected" + std::to_string(caseIndex++);
            compiler->registerSources(&context, {{{name, {name + ".nomad", "return " + NomadString(source)}}}, {}});
            BOOST_TEST(!compiler->compileFunctions(&context));
            BOOST_REQUIRE(context.getFirstError() != nullptr);
            auto machine = runtime.createVirtualMachine();
            InterpreterContext evaluation(*machine);
            BOOST_TEST(!Interpreter{}.execute(source, evaluation));
            BOOST_TEST(evaluation.hasError());
        }
    }
    BOOST_TEST(runtime.getTypeByName("Same<int>") == nullptr);
    const auto id = compileConstruction(runtime, "return new Same 1 2");
    BOOST_TEST(runtime.getScriptFunction(id)->getReturnType() == runtime.getTypeByName("Same<int>"));
}

BOOST_AUTO_TEST_CASE(inferred_constructor_arguments_preserve_execution_mode_evaluation_order) {
    Runtime runtime;
    std::vector<NomadInteger> markers;
    runtime.registerNativeFunction(
        "mark",
        [&](VirtualMachine* machine) {
            const auto value = machine->getIntegerParameter(0);
            markers.push_back(value);
            machine->setIntegerResult(value);
        },
        {defParameter("value", runtime.getIntegerType(), "")}, runtime.getIntegerType(), ""
    );
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Pair", 2,
        [](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, "Pair<int int>", ObjectTypeMode::Closed, arguments);
        },
        [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
            const auto* objectType = static_cast<const ObjectType*>(type);
            BOOST_REQUIRE(runtime.registerNativeConstructor(
                objectType,
                {defParameter("first", runtime.getIntegerType(), ""), defParameter("second", runtime.getIntegerType(), "")},
                [](const std::span<const RuntimeValue> arguments) {
                    BOOST_TEST(arguments[0].getIntegerValue() == 1);
                    BOOST_TEST(arguments[1].getIntegerValue() == 2);
                    return new ConstructedObject;
                }, ""
            ).isValid());
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Pair",
        {defTemplateParameter("first", TypeParameterIndex{0}, ""), defTemplateParameter("second", TypeParameterIndex{1}, "")}
    ));
    const auto id = compileConstruction(runtime, "return new Pair (mark 1) (mark 2)");
    BOOST_TEST(markers.empty());
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    result.freeObjectValue();
    BOOST_REQUIRE(markers.size() == 2u);
    BOOST_TEST(markers[0] == 2);
    BOOST_TEST(markers[1] == 1);
    markers.clear();
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_REQUIRE(Interpreter{}.execute("new Pair (mark 1) (mark 2)", context));
    BOOST_REQUIRE(markers.size() == 2u);
    BOOST_TEST(markers[0] == 1);
    BOOST_TEST(markers[1] == 2);
}

BOOST_AUTO_TEST_CASE(inferred_constructors_require_a_matching_specialized_constructor) {
    for (const auto registerConstructor : {false, true}) {
        Runtime runtime;
        BOOST_REQUIRE(runtime.registerParameterizedType(
            "Box", 1,
            [](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
                return std::make_unique<ObjectType>(owningRuntime, "Box<int>", ObjectTypeMode::Closed, arguments);
            },
            [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
                if (registerConstructor) {
                    BOOST_REQUIRE(runtime.registerNativeConstructor(
                        static_cast<const ObjectType*>(type),
                        {defParameter("value", runtime.getStringType(), "")},
                        [](std::span<const RuntimeValue>) { return new ConstructedObject; }, ""
                    ).isValid());
                }
            }
        ));
        BOOST_REQUIRE(runtime.registerConstructorTemplate(
            "Box", {defTemplateParameter("value", TypeParameterIndex{0}, "")}
        ));
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{{"main", {"main.nomad", "return new Box 42"}}}, {}});
        BOOST_TEST(!compiler->compileFunctions(&context));
        BOOST_REQUIRE(context.getFirstError() != nullptr);
        auto machine = runtime.createVirtualMachine();
        InterpreterContext evaluation(*machine);
        BOOST_TEST(!Interpreter{}.execute("new Box 42", evaluation));
        BOOST_TEST(evaluation.hasError());
    }
}

BOOST_AUTO_TEST_CASE(inferred_constructors_parse_from_signatures_without_specializing) {
    Runtime runtime;
    auto factoryCalls = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Pair", 2,
        [&factoryCalls](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
            ++factoryCalls;
            return std::make_unique<ObjectType>(owningRuntime, "Pair", ObjectTypeMode::Closed, arguments);
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Pair",
        {
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("value", TypeParameterIndex{1}, "")
        }
    ));
    for (const auto* source : {
        "new Pair \"key\" 1", "new Pair 123 true", "new Pair name value",
        "new Pair \"outer\" new Pair \"inner\" 42",
        "new Pair (1 + 2) (3 * 4)", "(new Pair \"key\" 1)", "(new Pair \"key\" 1)[0]"
    }) {
        BOOST_TEST_CONTEXT(source) {
            auto compiler = runtime.createCompiler();
            CompilerContext context(compiler.get());
            Tokenizer tokens(&context, "test.nomad", source);
            auto expression = parser::parseExpression(&context, nullptr, &tokens);
            BOOST_REQUIRE(expression != nullptr);
            BOOST_TEST(!context.hasError());
            BOOST_TEST(tokens.endOfLine());
        }
    }
    for (const auto* boundary : {")", "]", "extra"}) {
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        Tokenizer tokens(&context, "test.nomad", "new Pair \"key\" 1 " + NomadString(boundary));
        auto expression = parser::parseNewExpression(&context, nullptr, &tokens, nullptr);
        BOOST_REQUIRE(expression != nullptr);
        BOOST_TEST(tokens.is(boundary));
        BOOST_TEST(!context.hasError());
    }
    BOOST_TEST(factoryCalls == 0u);
    BOOST_TEST(runtime.getConstructors().empty());
}

BOOST_AUTO_TEST_CASE(inferred_constructor_parsing_reports_missing_arguments_and_signatures) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Pair", 2, [](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, "Pair", ObjectTypeMode::Closed, arguments);
        }
    ));
    auto compiler = runtime.createCompiler();
    {
        CompilerContext context(compiler.get());
        Tokenizer tokens(&context, "test.nomad", "new Pair \"key\" 1");
        BOOST_TEST(parser::parseExpression(&context, nullptr, &tokens) == nullptr);
        BOOST_REQUIRE(context.getFirstError() != nullptr);
        BOOST_TEST(context.getFirstError()->message.find("no inference signature") != NomadString::npos);
    }
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Pair",
        {
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("value", TypeParameterIndex{1}, "")
        }
    ));
    for (const auto* source : {"new Pair", "new Pair \"key\"", "new Pair \"key\" )", "new Pair \"key\" ]"}) {
        CompilerContext context(compiler.get());
        Tokenizer tokens(&context, "test.nomad", source);
        BOOST_TEST(parser::parseExpression(&context, nullptr, &tokens) == nullptr);
        BOOST_REQUIRE(context.getFirstError() != nullptr);
        BOOST_TEST(context.getFirstError()->message.find("Expected arguments for") != NomadString::npos);
    }
}

BOOST_AUTO_TEST_CASE(inferred_constructor_parsing_reuses_concrete_and_hidden_parameter_shapes) {
    Runtime runtime;
    auto calls = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Located", 1, [](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, "Located<" + arguments[0]->getTypeName() + ">", ObjectTypeMode::Closed, arguments
            );
        },
        [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
            const auto* objectType = static_cast<const ObjectType*>(type);
            BOOST_REQUIRE(runtime.registerNativeConstructor(
                objectType,
                {
                    defParameter("file", runtime.getFileNameType(), ""),
                    defParameter("value", objectType->getTypeArguments()[0], ""),
                    defParameter("enabled", runtime.getBooleanType(), ""),
                    defParameter("function", runtime.getFunctionNameType(), ""),
                    defParameter("line", runtime.getLineNumberType(), "")
                },
                [&](const std::span<const RuntimeValue> arguments) {
                    BOOST_REQUIRE(arguments.size() == 5u);
                    BOOST_TEST(arguments[1].getStringObject()->getString() == "value");
                    BOOST_TEST(arguments[2].getBooleanValue());
                    ++calls;
                    return new ConstructedObject;
                }, ""
            ).isValid());
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Located",
        {
            defTemplateParameter("file", runtime.getFileNameType(), ""),
            defTemplateParameter("value", TypeParameterIndex{0}, ""),
            defTemplateParameter("enabled", runtime.getBooleanType(), ""),
            defTemplateParameter("function", runtime.getFunctionNameType(), ""),
            defTemplateParameter("line", runtime.getLineNumberType(), "")
        }
    ));
    const auto id = compileConstruction(runtime, "return 0");
    auto* function = runtime.getScriptFunction(id);
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    Tokenizer tokens(&context, "test.nomad", "new Located \"value\" true");
    auto expression = parser::parseExpression(&context, function, &tokens);
    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(tokens.endOfLine());
    BOOST_TEST(!context.hasError());
    BOOST_TEST(runtime.getTypeByName("Located<string>") == nullptr);
    const auto inferredId = compileConstruction(runtime, "return new Located \"value\" true", "located");
    RuntimeValue result;
    runtime.executeFunction(inferredId, {}, result);
    result.freeObjectValue();
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext evaluation(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("new Located \"value\" true", evaluation));
        BOOST_TEST(evaluation.getResult()->getType() == runtime.getTypeByName("Located<string>"));
    }
    BOOST_TEST(calls == 2u);
}

BOOST_AUTO_TEST_CASE(constructor_templates_store_generic_and_concrete_parameters_without_specializing) {
    Runtime runtime;
    auto factoryCalls = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Example", 2,
        [&factoryCalls](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
            ++factoryCalls;
            return std::make_unique<ObjectType>(owningRuntime, "Example", ObjectTypeMode::Closed, arguments);
        }
    ));
    BOOST_TEST(!runtime.getConstructorTemplate("Missing").has_value());
    BOOST_TEST(!runtime.getConstructorTemplate("Example").has_value());
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Example",
        {
            defTemplateParameter("key", TypeParameterIndex{0}, "The key."),
            defTemplateParameter("value", TypeParameterIndex{1}, "The value."),
            defTemplateParameter("sameKey", TypeParameterIndex{0}, ""),
            defTemplateParameter("enabled", runtime.getBooleanType(), "Whether enabled."),
            defTemplateParameter("file", runtime.getFileNameType(), "")
        }
    ));
    auto signature = runtime.getConstructorTemplate("Example");
    BOOST_REQUIRE(signature.has_value());
    BOOST_REQUIRE(signature->size() == 5u);
    BOOST_TEST((*signature)[0].name == "key");
    BOOST_TEST((*signature)[0].doc == "The key.");
    BOOST_TEST(std::get<TypeParameterIndex>((*signature)[0].type).index == 0u);
    BOOST_TEST(std::get<TypeParameterIndex>((*signature)[1].type).index == 1u);
    BOOST_TEST(std::get<TypeParameterIndex>((*signature)[2].type).index == 0u);
    BOOST_TEST(std::get<const Type*>((*signature)[3].type) == runtime.getBooleanType());
    BOOST_TEST(std::get<const Type*>((*signature)[4].type) == runtime.getFileNameType());
    signature->clear();
    BOOST_TEST(runtime.getConstructorTemplate("Example")->size() == 5u);
    BOOST_TEST(factoryCalls == 0u);
    BOOST_TEST(runtime.getTypeByName("Example") == nullptr);
}

BOOST_AUTO_TEST_CASE(constructor_templates_reject_invalid_definitions_without_reserving_signature) {
    Runtime runtime;
    Runtime other;
    const auto factory = [](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
        return std::make_unique<ObjectType>(owningRuntime, "Example", ObjectTypeMode::Closed, arguments);
    };
    BOOST_REQUIRE(runtime.registerParameterizedType("Example", 2, factory));
    BOOST_TEST(!runtime.registerConstructorTemplate(
        "Missing", {defTemplateParameter("value", TypeParameterIndex{0}, "")}
    ));
    BOOST_TEST(!runtime.registerConstructorTemplate("Example", {}));
    for (const auto& parameters : std::vector<std::vector<TemplateParameter>>{
        {defTemplateParameter("value", TypeParameterIndex{0}, "")},
        {defTemplateParameter("key", TypeParameterIndex{0}, ""), defTemplateParameter("value", TypeParameterIndex{2}, "")},
        {
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("value", TypeParameterIndex{1}, ""),
            defTemplateParameter("invalid", static_cast<const Type*>(nullptr), "")
        },
        {
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("value", TypeParameterIndex{1}, ""),
            defTemplateParameter("invalid", runtime.getVoidType(), "")
        },
        {
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("value", TypeParameterIndex{1}, ""),
            defTemplateParameter("invalid", other.getIntegerType(), "")
        }
    }) {
        BOOST_TEST(!runtime.registerConstructorTemplate("Example", parameters));
        BOOST_TEST(!runtime.getConstructorTemplate("Example").has_value());
    }
    BOOST_REQUIRE(runtime.registerNativeParameterizedConstructor(
        "Alias", 1, [](const NomadString&, const std::vector<const Type*>&) {}
    ));
    BOOST_TEST(!runtime.registerConstructorTemplate(
        "Alias", {defTemplateParameter("value", TypeParameterIndex{0}, "")}
    ));
    registerObjectType(runtime, "Concrete");
    BOOST_TEST(!runtime.registerConstructorTemplate(
        "Concrete", {defTemplateParameter("value", TypeParameterIndex{0}, "")}
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Example",
        {defTemplateParameter("key", TypeParameterIndex{0}, ""), defTemplateParameter("value", TypeParameterIndex{1}, "")}
    ));
    BOOST_TEST(!runtime.registerConstructorTemplate(
        "Example",
        {defTemplateParameter("value", TypeParameterIndex{1}, ""), defTemplateParameter("key", TypeParameterIndex{0}, "")}
    ));
    const auto signature = runtime.getConstructorTemplate("Example");
    BOOST_REQUIRE(signature.has_value());
    BOOST_TEST((*signature)[0].name == "key");
    BOOST_TEST(std::get<TypeParameterIndex>((*signature)[0].type).index == 0u);
}

BOOST_AUTO_TEST_CASE(constructor_template_registration_preserves_explicit_construction) {
    Runtime runtime;
    auto calls = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Box", 1,
        [](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
            const auto* elementType = arguments[0];
            return std::make_unique<ObjectType>(owningRuntime, "Box<" + elementType->getTypeName() + ">", ObjectTypeMode::Closed, arguments);
        },
        [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
            const auto* objectType = static_cast<const ObjectType*>(type);
            const auto* elementType = objectType->getTypeArguments()[0];
            BOOST_REQUIRE(runtime.registerNativeConstructor(
                objectType, {defParameter("value", elementType, "")},
                [&](const std::span<const RuntimeValue> arguments) {
                    BOOST_TEST(arguments[0].getIntegerValue() == 42);
                    ++calls;
                    return new ConstructedObject;
                }, ""
            ).isValid());
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Box", {defTemplateParameter("value", TypeParameterIndex{0}, "")}
    ));
    const auto id = compileConstruction(runtime, "return new Box<int> 42");
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    result.freeObjectValue();
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("new Box<int> 42", context));
    }
    BOOST_TEST(calls == 2u);
}

BOOST_AUTO_TEST_CASE(constructor_inference_binds_static_types_without_creating_specializations) {
    Runtime runtime;
    auto factoryCalls = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Pair", 2,
        [&factoryCalls](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
            ++factoryCalls;
            return std::make_unique<ObjectType>(owningRuntime, "Pair", ObjectTypeMode::Closed, arguments);
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Pair",
        {
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("value", TypeParameterIndex{1}, "")
        }
    ));
    for (const auto& argumentTypes : std::vector<std::vector<const Type*>>{
        {runtime.getStringType(), runtime.getIntegerType()},
        {runtime.getIntegerType(), runtime.getBooleanType()},
        {runtime.getBooleanType(), runtime.getFloatType()}
    }) {
        const auto result = runtime.inferConstructorTypeArguments("Pair", argumentTypes);
        BOOST_REQUIRE(result.isResolved());
        BOOST_CHECK(result.status == ResolveStatus::Resolved);
        BOOST_TEST(result.error.empty());
        BOOST_REQUIRE(result.typeArguments.size() == 2u);
        BOOST_TEST(result.typeArguments[0] == argumentTypes[0]);
        BOOST_TEST(result.typeArguments[1] == argumentTypes[1]);
    }
    const auto* nested = registerObjectType(runtime, "List<int>");
    const auto result = runtime.inferConstructorTypeArguments("Pair", {runtime.getStringType(), nested});
    BOOST_REQUIRE(result.isResolved());
    BOOST_TEST(result.typeArguments[1] == nested);
    BOOST_TEST(factoryCalls == 0u);
    BOOST_TEST(runtime.getConstructors().empty());
    BOOST_TEST(runtime.getTypeByName("Pair<string int>") == nullptr);
}

BOOST_AUTO_TEST_CASE(constructor_inference_supports_reordered_repeated_and_concrete_parameters) {
    Runtime runtime;
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Example", 2, [](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, "Example", ObjectTypeMode::Closed, arguments);
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Example",
        {
            defTemplateParameter("value", TypeParameterIndex{1}, ""),
            defTemplateParameter("key", TypeParameterIndex{0}, ""),
            defTemplateParameter("sameValue", TypeParameterIndex{1}, ""),
            defTemplateParameter("enabled", runtime.getBooleanType(), ""),
            defTemplateParameter("file", runtime.getFileNameType(), ""),
            defTemplateParameter("function", runtime.getFunctionNameType(), ""),
            defTemplateParameter("line", runtime.getLineNumberType(), "")
        }
    ));
    const auto result = runtime.inferConstructorTypeArguments(
        "Example",
        {
            runtime.getIntegerType(), runtime.getStringType(), runtime.getIntegerType(),
            runtime.getBooleanType(), nullptr, nullptr, nullptr
        }
    );
    BOOST_REQUIRE(result.isResolved());
    BOOST_REQUIRE(result.typeArguments.size() == 2u);
    BOOST_TEST(result.typeArguments[0] == runtime.getStringType());
    BOOST_TEST(result.typeArguments[1] == runtime.getIntegerType());
}

BOOST_AUTO_TEST_CASE(constructor_inference_reports_failures_without_partial_bindings) {
    Runtime runtime;
    Runtime other;
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Example", 1, [](Runtime* owningRuntime, const std::vector<const Type*>& arguments) {
            return std::make_unique<ObjectType>(owningRuntime, "Example", ObjectTypeMode::Closed, arguments);
        }
    ));
    const auto unknown = runtime.inferConstructorTypeArguments("Missing", {});
    BOOST_CHECK(unknown.status == ResolveStatus::UnknownName);
    BOOST_TEST(!unknown.isResolved());
    BOOST_TEST(unknown.typeArguments.empty());
    BOOST_TEST(unknown.error.find("Missing") != NomadString::npos);
    const auto missingSignature = runtime.inferConstructorTypeArguments("Example", {});
    BOOST_CHECK(missingSignature.status == ResolveStatus::UnknownName);
    BOOST_TEST(!missingSignature.error.empty());
    BOOST_REQUIRE(runtime.registerConstructorTemplate(
        "Example",
        {
            defTemplateParameter("first", TypeParameterIndex{0}, ""),
            defTemplateParameter("second", TypeParameterIndex{0}, ""),
            defTemplateParameter("number", runtime.getIntegerType(), "")
        }
    ));
    for (const auto& argumentTypes : std::vector<std::vector<const Type*>>{
        {}, {runtime.getIntegerType()},
        {runtime.getIntegerType(), runtime.getIntegerType(), runtime.getIntegerType(), runtime.getIntegerType()}
    }) {
        const auto result = runtime.inferConstructorTypeArguments("Example", argumentTypes);
        BOOST_CHECK(result.status == ResolveStatus::NoMatchingArity);
        BOOST_TEST(!result.isResolved());
        BOOST_TEST(result.typeArguments.empty());
        BOOST_TEST(result.error.find("expects 3 argument(s)") != NomadString::npos);
    }
    for (const auto* invalid : {
        static_cast<const Type*>(nullptr), static_cast<const Type*>(runtime.getVoidType()),
        runtime.getFileNameType(), static_cast<const Type*>(other.getIntegerType())
    }) {
        const auto result = runtime.inferConstructorTypeArguments(
            "Example", {runtime.getIntegerType(), invalid, runtime.getIntegerType()}
        );
        BOOST_CHECK(result.status == ResolveStatus::NoMatchingTypes);
        BOOST_TEST(result.typeArguments.empty());
        BOOST_TEST(result.error.find("argument 2 ('second')") != NomadString::npos);
        BOOST_TEST(result.error.find("explicit type arguments") != NomadString::npos);
    }
    const auto conflict = runtime.inferConstructorTypeArguments(
        "Example", {runtime.getIntegerType(), runtime.getFloatType(), runtime.getIntegerType()}
    );
    BOOST_CHECK(conflict.status == ResolveStatus::NoMatchingTypes);
    BOOST_TEST(conflict.typeArguments.empty());
    BOOST_TEST(conflict.error.find("conflicting types for type parameter 0") != NomadString::npos);
    BOOST_TEST(conflict.error.find("'int'") != NomadString::npos);
    BOOST_TEST(conflict.error.find("'float'") != NomadString::npos);
    for (const auto* incompatible : {
        static_cast<const Type*>(nullptr), static_cast<const Type*>(runtime.getFloatType()),
        static_cast<const Type*>(other.getIntegerType())
    }) {
        const auto result = runtime.inferConstructorTypeArguments(
            "Example", {runtime.getIntegerType(), runtime.getIntegerType(), incompatible}
        );
        BOOST_CHECK(result.status == ResolveStatus::NoMatchingTypes);
        BOOST_TEST(result.typeArguments.empty());
        BOOST_TEST(result.error.find("argument 3 ('number')") != NomadString::npos);
        BOOST_TEST(result.error.find("expected exact type 'int'") != NomadString::npos);
    }
    const auto recovered = runtime.inferConstructorTypeArguments(
        "Example", {runtime.getIntegerType(), runtime.getIntegerType(), runtime.getIntegerType()}
    );
    BOOST_REQUIRE(recovered.isResolved());
    BOOST_REQUIRE(recovered.typeArguments.size() == 1u);
    BOOST_TEST(recovered.typeArguments[0] == runtime.getIntegerType());
}

BOOST_AUTO_TEST_CASE(constructors_accept_heterogeneous_arguments_and_type_specific_zero_values) {
    Runtime runtime;
    auto calls = NomadIndex{0};
    const auto* type = registerObjectType(runtime, "Example");
    BOOST_REQUIRE(runtime.registerNativeConstructor(
        type,
        {
            defParameter("number", runtime.getIntegerType(), ""),
            defParameter("text", runtime.getStringType(), ""),
            defParameter("enabled", runtime.getBooleanType(), "")
        },
        [&calls](const std::span<const RuntimeValue> arguments) {
            BOOST_REQUIRE(arguments.size() == 3u);
            BOOST_TEST(arguments[0].getIntegerValue() == 0);
            BOOST_TEST(arguments[1].getStringObject()->getString() == "hello");
            BOOST_TEST(arguments[2].getBooleanValue());
            ++calls;
            return new ConstructedObject;
        }, ""
    ).isValid());
    const auto id = compileConstruction(runtime, "return new Example 0 \"hello\" true");
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(result.getObjectValue() != nullptr);
    result.freeObjectValue();
    BOOST_TEST(calls == 1u);
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_REQUIRE(Interpreter{}.execute("new Example 0 \"hello\" true", context));
        BOOST_TEST(calls == 2u);
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(constructor_faults_and_null_factories_release_object_arguments) {
    for (const auto returnsNull : {false, true}) {
        Runtime runtime;
        const auto* type = registerObjectType(runtime, "Example");
        BOOST_REQUIRE(runtime.registerNativeConstructor(
            type, {defParameter("text", runtime.getStringType(), "")},
            [returnsNull](std::span<const RuntimeValue>) -> NomadObject* {
                if (returnsNull) {
                    return nullptr;
                }
                throw NomadException("Constructor failed");
            }, ""
        ).isValid());
        const auto id = compileConstruction(runtime, "return new Example (\"hello\".toUpper)");
        const auto before = NomadObject::getLiveObjectCount();
        auto machine = runtime.createVirtualMachine();
        for (auto attempt = 0; attempt < 2; ++attempt) {
            BOOST_CHECK_THROW(machine->run(runtime.getScriptFunction(id), {}), NomadException);
            BOOST_TEST(NomadObject::getLiveObjectCount() == before);
        }
        {
            InterpreterContext context(*machine);
            BOOST_TEST(!Interpreter{}.execute("new Example (\"hello\".toUpper)", context));
            BOOST_REQUIRE(context.getError() != nullptr);
        }
        BOOST_TEST(NomadObject::getLiveObjectCount() == before);
    }
}

BOOST_AUTO_TEST_CASE(parameterless_constructors_are_explicitly_registered) {
    Runtime runtime;
    const auto* type = registerObjectType(runtime, "Plain");
    BOOST_REQUIRE(runtime.registerNativeConstructor(
        type, {}, [](std::span<const RuntimeValue>) { return new ConstructedObject; }, ""
    ).isValid());
    const auto id = compileConstruction(runtime, "return new Plain");
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(result.getObjectValue() != nullptr);
}

BOOST_AUTO_TEST_CASE(source_parameters_are_supplied_by_the_compiler) {
    Runtime runtime;
    const auto* type = registerObjectType(runtime, "Located");
    NomadString file;
    NomadString functionName;
    NomadInteger line = 0;
    NomadInteger number = 0;
    BOOST_REQUIRE(runtime.registerNativeConstructor(
        type,
        {
            defParameter("file", runtime.getFileNameType(), ""),
            defParameter("number", runtime.getIntegerType(), ""),
            defParameter("function", runtime.getFunctionNameType(), ""),
            defParameter("line", runtime.getLineNumberType(), "")
        },
        [&](const std::span<const RuntimeValue> arguments) {
            BOOST_REQUIRE(arguments.size() == 4u);
            file = arguments[0].getStringObject()->getString();
            number = arguments[1].getIntegerValue();
            functionName = arguments[2].getStringObject()->getString();
            line = arguments[3].getIntegerValue();
            return new ConstructedObject;
        }, ""
    ).isValid());

    const auto id = compileConstruction(runtime, "\nreturn new Located 42");
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    BOOST_REQUIRE(result.getObjectValue() != nullptr);
    result.freeObjectValue();
    BOOST_TEST(file == "main.nomad");
    BOOST_TEST(number == 42);
    BOOST_TEST(functionName == "main");
    BOOST_TEST(line == 2);

    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);
    BOOST_TEST(Interpreter{}.execute("new Located 7", context));
    BOOST_TEST(number == 7);

    auto compiler = runtime.createCompiler();
    CompilerContext compilerContext(compiler.get());
    compiler->registerSources(&compilerContext, {{{"invalid", {"invalid.nomad", "return new Located 1 2"}}}, {}});
    BOOST_TEST(!compiler->compileFunctions(&compilerContext));
}

BOOST_AUTO_TEST_CASE(registration_rejects_collisions_invalid_types_and_missing_factories) {
    Runtime runtime;
    Runtime other;
    const auto* type = registerObjectType(runtime, "Example");
    const auto* otherType = registerObjectType(runtime, "Other");
    const auto factory = [](std::span<const RuntimeValue>) { return new ConstructedObject; };
    const auto parameters = std::vector{defParameter("value", runtime.getIntegerType(), "")};
    const auto id = runtime.registerNativeConstructor(type, parameters, factory, "");
    BOOST_REQUIRE(id.isValid());
    BOOST_TEST(runtime.registerNativeConstructor(type, parameters, factory, "").isInvalid());
    BOOST_TEST(runtime.registerNativeConstructor(type, parameters, otherType, factory, "").isInvalid());
    BOOST_TEST(runtime.registerNativeConstructor(
        type, {defParameter("renamed", runtime.getIntegerType(), "")}, factory, ""
    ).isInvalid());
    BOOST_TEST(runtime.registerNativeConstructor(
        type, {defParameter("value", other.getIntegerType(), "")}, factory, ""
    ).isInvalid());
    for (const auto* invalid : {runtime.getVoidType(), static_cast<const Type*>(nullptr)}) {
        BOOST_TEST(runtime.registerNativeConstructor(
            otherType, {defParameter("value", invalid, "")}, factory, ""
        ).isInvalid());
    }
    BOOST_TEST(runtime.registerNativeConstructor(nullptr, {}, factory, "").isInvalid());
    BOOST_TEST(runtime.registerNativeConstructor(registerObjectType(other, "Foreign"), {}, factory, "").isInvalid());
    BOOST_TEST(runtime.registerNativeConstructor(
        type, {}, registerObjectType(other, "ForeignCreated"), factory, ""
    ).isInvalid());
    BOOST_TEST(runtime.registerNativeConstructor(type, {}, {}, "").isInvalid());
    BOOST_TEST(runtime.getConstructors().size() == 1u);
    const auto* constructor = runtime.getConstructor(id);
    BOOST_REQUIRE(constructor != nullptr);
    BOOST_TEST(constructor->getReturnType() == type);
    BOOST_TEST(runtime.getConstructor(CallableId::INVALID) == nullptr);
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"invalid", {"invalid.nomad", "return new Example true"}}}, {}});
    BOOST_TEST(!compiler->compileFunctions(&context));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->sourceName == "invalid.nomad");
    BOOST_TEST(context.getFirstError()->message.find("No overload of 'Example'") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(overloads_select_by_argument_types_and_count_with_distinct_return_types) {
    Runtime runtime;
    const auto* singleType = registerObjectType(runtime, "Single");
    const auto* pairType = registerObjectType(runtime, "Pair");
    const auto* textType = registerObjectType(runtime, "Text");
    const auto* emptyType = registerObjectType(runtime, "Empty");
    const auto* exampleType = registerObjectType(runtime, "Example");
    NomadString calls;
    const auto registerFactory = [&](const std::vector<Parameter>& parameters,
                                     const ObjectType* type, const NomadString& marker) {
        BOOST_REQUIRE(runtime.registerNativeConstructor(
            exampleType, parameters, type,
            [&calls, marker](std::span<const RuntimeValue>) {
                calls += marker;
                return new ConstructedObject;
            }, ""
        ).isValid());
    };
    registerFactory({}, emptyType, "E");
    registerFactory({defParameter("value", runtime.getIntegerType(), "")}, singleType, "I");
    registerFactory(
        {defParameter("first", runtime.getIntegerType(), ""), defParameter("second", runtime.getIntegerType(), "")},
        pairType, "P"
    );
    registerFactory({defParameter("value", runtime.getStringType(), "")}, textType, "S");
    runtime.registerNativeFunction(
        "consume",
        [](VirtualMachine* machine) { machine->setIntegerResult(machine->getIntegerParameter(1)); },
        {defParameter("object", singleType, ""), defParameter("value", runtime.getIntegerType(), "")},
        runtime.getIntegerType(), ""
    );
    const auto id = compileConstruction(runtime,
        "empty = (new Example)\n"
        "single = (new Example 1)\n"
        "pair = new Example 1 2\n"
        "text = new Example \"hello\"\n"
        "assert (consume (new Example 1) 2) == 2\n"
        "return pair"
    );
    const auto before = NomadObject::getLiveObjectCount();
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    result.freeObjectValue();
    BOOST_TEST(calls == "EIPSI");
    {
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        for (const auto* source : {"new Example", "(new Example 1)", "new Example 1 2", "new Example \"hello\""}) {
            BOOST_REQUIRE(Interpreter{}.execute(source, context));
        }
        BOOST_TEST(context.getResult()->getType() == textType);
        BOOST_REQUIRE(Interpreter{}.execute("new Example 1 2", context));
        BOOST_TEST(context.getResult()->getType() == pairType);
        BOOST_REQUIRE(Interpreter{}.execute("(new Example 1)", context));
        BOOST_TEST(context.getResult()->getType() == singleType);
        BOOST_REQUIRE(Interpreter{}.execute("consume (new Example 1) 2", context));
        BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 2);
        BOOST_TEST(!Interpreter{}.execute("new Example true", context));
        BOOST_TEST(!Interpreter{}.execute("new Example 1 true", context));
    }
    BOOST_TEST(NomadObject::getLiveObjectCount() == before);
}

BOOST_AUTO_TEST_CASE(parameterized_constructor_registration_validates_and_reuses_specializations) {
    Runtime runtime;
    Runtime other;
    auto calls = NomadIndex{0};
    const auto initializer = [&](const NomadString& name, const std::vector<const Type*>& arguments) {
        ++calls;
        const auto* classType = registerObjectType(runtime, name);
        BOOST_REQUIRE(runtime.registerNativeConstructor(
            classType, {defParameter("value", arguments[0], "")},
            [](std::span<const RuntimeValue>) { return new ConstructedObject; }, ""
        ).isValid());
    };
    BOOST_TEST(!runtime.registerNativeParameterizedConstructor("", 1, initializer));
    BOOST_TEST(!runtime.registerNativeParameterizedConstructor("Create", 0, initializer));
    BOOST_TEST(!runtime.registerNativeParameterizedConstructor("Create", 1, {}));
    BOOST_TEST(!runtime.registerNativeParameterizedConstructor("int", 1, initializer));
    BOOST_REQUIRE(runtime.registerNativeParameterizedConstructor("Create", 1, initializer));
    BOOST_TEST(!runtime.registerNativeParameterizedConstructor("Create", 1, initializer));
    BOOST_TEST(runtime.registerNativeConstructor(
        registerObjectType(runtime, "Create"), {}, [](std::span<const RuntimeValue>) { return new ConstructedObject; }, ""
    ).isInvalid());
    BOOST_TEST(!runtime.registerParameterizedType("Create", 1, [](Runtime* owningRuntime, const auto&) {
        return std::make_unique<ObjectType>(owningRuntime, "Collision", ObjectTypeMode::Closed);
    }));
    BOOST_TEST(!runtime.getParameterizedConstructorArgumentCount("Missing").has_value());
    BOOST_REQUIRE(runtime.getParameterizedConstructorArgumentCount("Create").has_value());
    BOOST_TEST(*runtime.getParameterizedConstructorArgumentCount("Create") == 1u);
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {runtime.getIntegerType()}) == "Create<int>");
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {runtime.getIntegerType()}) == "Create<int>");
    BOOST_TEST(calls == 1u);
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {other.getIntegerType()}).empty());
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {}).empty());
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {runtime.getVoidType()}).empty());
    BOOST_TEST(runtime.getParameterizedConstructorName("Create", {nullptr}).empty());
    BOOST_TEST(runtime.getParameterizedConstructorName("Missing", {runtime.getIntegerType()}).empty());
    BOOST_TEST(calls == 1u);
    const auto id = compileConstruction(runtime, "return new Create<int> 42");
    RuntimeValue result;
    runtime.executeFunction(id, {}, result);
    result.freeObjectValue();
    BOOST_TEST(calls == 1u);
}

BOOST_AUTO_TEST_CASE(failed_parameterized_initialization_is_not_cached_as_success) {
    Runtime runtime;
    auto attempts = 0;
    BOOST_REQUIRE(runtime.registerNativeParameterizedConstructor(
        "Create", 1,
        [&](const NomadString& name, const std::vector<const Type*>&) {
            if (attempts++ == 0) {
                BOOST_REQUIRE(runtime.registerNativeConstructor(
                    registerObjectType(runtime, name), {},
                    [](std::span<const RuntimeValue>) { return new ConstructedObject; }, ""
                ).isValid());
            }
            throw NomadException("Initialization failed");
        }
    ));
    for (auto attempt = 0; attempt < 2; ++attempt) {
        BOOST_CHECK_THROW(
            static_cast<void>(runtime.getParameterizedConstructorName("Create", {runtime.getIntegerType()})),
            NomadException
        );
    }
    BOOST_TEST(attempts == 2);
}

BOOST_AUTO_TEST_SUITE_END()
