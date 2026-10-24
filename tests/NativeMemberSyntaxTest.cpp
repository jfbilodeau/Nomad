// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <boost/test/unit_test.hpp>

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptCallable.hpp>
#include <nomad/script/VariableContext.hpp>

using namespace nomad;

namespace {

NativePropertySetFn propertySetter() {
    return [](VirtualMachine*, const RuntimeValue&) {};
}

NativeFunctionFn methodFunction() {
    return [](VirtualMachine*) {};
}

std::unique_ptr<Expression> parseExpression(CompilerContext& context, ScriptFunction* function, const NomadString& source) {
    Tokenizer tokens(&context, "test.nomad", source);
    auto expression = parser::parseExpression(&context, function, &tokens);

    if (expression && !tokens.endOfLine()) {
        context.reportError("Test expression was not fully parsed", &tokens);
    }

    return expression;
}

} // namespace

BOOST_AUTO_TEST_SUITE(native_member_syntax)

BOOST_AUTO_TEST_CASE(parses_family_members_before_specialization_and_resolves_concrete_members) {
    Runtime runtime;
    auto specializations = NomadIndex{0};
    BOOST_REQUIRE(runtime.registerParameterizedType(
        "Box", 1,
        [&](Runtime* owningRuntime, TypeId id, const std::vector<const Type*>& arguments) {
            ++specializations;
            return std::make_unique<ObjectType>(owningRuntime, id, "Box<" + arguments[0]->getTypeName() + ">", ObjectTypeMode::Closed, arguments);
        },
        [&](Runtime* owningRuntime, const TypeId id) {
            const auto* type = owningRuntime->getType(id).value();
            const auto* objectType = static_cast<const ObjectType*>(type);
            const auto* valueType = objectType->getTypeArguments()[0];
            BOOST_REQUIRE(runtime.registerNativeConstructor(
                objectType, {defParameter("value", valueType, "")},
                [](std::span<const RuntimeValue>) { return new NomadObject; }, ""
            ).isValid());
            BOOST_REQUIRE(runtime.addProperty(objectType->getId(), {
                .valueTypeId = valueType->getId(),
                .name = "declaredProperty",
                .getter = [](VirtualMachine*, RuntimeValue&) {},
                .setter = {},
                .documentation = ""
            }).isValid());
            BOOST_REQUIRE(runtime.addMethod(objectType->getId(), {
                .name = "declaredMethod",
                .parameters = {
                    {valueType->getId(), "value", ""},
                    {runtime.getLineNumberType()->getId(), "line", ""}
                },
                .returnTypeId = valueType->getId(),
                .function = methodFunction(),
                .documentation = ""
            }).isValid());
        }
    ));
    BOOST_REQUIRE(runtime.registerConstructorTemplate("Box", {
        defTemplateParameter("value", TypeParameterIndex{0}, "")
    }));
    BOOST_REQUIRE(runtime.registerMemberTemplate("Box", "declaredProperty", MemberType::Property));
    BOOST_REQUIRE(runtime.registerMemberTemplate("Box", "declaredMethod", MemberType::Method, {
        defTemplateParameter("value", TypeParameterIndex{0}, ""),
        defTemplateParameter("line", runtime.getLineNumberType(), "")
    }));
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    auto property = parseExpression(context, nullptr, "(new Box 1).declaredProperty");
    auto method = parseExpression(context, nullptr, "(new Box 1).declaredMethod 2");
    BOOST_REQUIRE(property != nullptr);
    BOOST_REQUIRE(method != nullptr);
    BOOST_TEST(context.getErrorCount() == 0);
    BOOST_TEST(specializations == 0u);
    BOOST_TEST(runtime.getNativeMemberOverloadCount("declaredProperty") == 0u);
    BOOST_REQUIRE(property->resolve(&context, nullptr));
    BOOST_REQUIRE(method->resolve(&context, nullptr));
    BOOST_TEST(property->getType() == runtime.getIntegerType());
    BOOST_TEST(method->getType() == runtime.getIntegerType());
    BOOST_TEST(specializations == 1u);
}

BOOST_AUTO_TEST_CASE(resolves_property_on_literal) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    auto expression = parseExpression(context, nullptr, "\"Nomad\".length");

    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(expression->resolve(&context, nullptr));
    BOOST_TEST(expression->getType() == runtime.getIntegerType());
    BOOST_TEST(context.getErrorCount() == 0);
}

BOOST_AUTO_TEST_CASE(resolves_property_on_qualified_local) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    ScriptFunction function(CallableId::INVALID, "test", "test.nomad", "");
    function.registerVariable("name", runtime.getStringType());

    auto expression = parseExpression(context, &function, "name.length");

    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(expression->resolve(&context, &function));
    BOOST_TEST(expression->getType() == runtime.getIntegerType());
}

BOOST_AUTO_TEST_CASE(resolves_method_arguments_using_receiver_type) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    ScriptFunction function(CallableId::INVALID, "test", "test.nomad", "");
    function.registerVariable("name", runtime.getStringType());

    auto expression = parseExpression(context, &function, "name.contains \"mad\"");

    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(expression->resolve(&context, &function));
    BOOST_TEST(expression->getType() == runtime.getBooleanType());
}

BOOST_AUTO_TEST_CASE(resolves_chained_members_on_function_return) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    const auto functionId = runtime.registerScriptFunction("getName", "getName.nomad", "return \"Nomad\"");
    runtime.getScriptFunction(functionId)->setReturnType(runtime.getStringType());

    auto expression = parseExpression(context, nullptr, "getName.toUpper.length");

    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(expression->resolve(&context, nullptr));
    BOOST_TEST(expression->getType() == runtime.getIntegerType());
}

BOOST_AUTO_TEST_CASE(rejects_reading_write_only_property) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    BOOST_REQUIRE(runtime.addProperty(runtime.getTypeId("string"), {
        .valueTypeId = runtime.getTypeId("int"),
        .name = "sink",
        .getter = {},
        .setter = propertySetter(),
        .documentation = NomadDoc("Sink")
    }).isValid());

    auto expression = parseExpression(context, nullptr, "\"Nomad\".sink");

    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(!expression->resolve(&context, nullptr));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->message.find("write-only") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(rejects_writing_read_only_property) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    ScriptFunction function(CallableId::INVALID, "test", "test.nomad", "");
    function.registerVariable("name", runtime.getStringType());

    Tokenizer tokens(&context, "test.nomad", "name.length = 10");
    auto statement = parser::parseLine(&context, &function, &tokens);

    BOOST_REQUIRE(statement != nullptr);
    BOOST_TEST(!statement->resolve(&context, &function));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->message.find("read-only") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(allows_assigning_write_only_property) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    ScriptFunction function(CallableId::INVALID, "test", "test.nomad", "");
    function.registerVariable("name", runtime.getStringType());
    BOOST_REQUIRE(runtime.addProperty(runtime.getTypeId("string"), {
        .valueTypeId = runtime.getTypeId("int"),
        .name = "sink",
        .getter = {},
        .setter = propertySetter(),
        .documentation = ""
    }).isValid());

    Tokenizer tokens(&context, "test.nomad", "name.sink = 10");
    auto statement = parser::parseLine(&context, &function, &tokens);

    BOOST_REQUIRE(statement != nullptr);
    BOOST_TEST(statement->resolve(&context, &function));
    BOOST_TEST(context.getErrorCount() == 0);
}

BOOST_AUTO_TEST_CASE(chained_assignment_reads_receiver_but_only_requires_final_setter) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    BOOST_REQUIRE(runtime.addProperty(runtime.getTypeId("string"), {
        .valueTypeId = runtime.getTypeId("int"),
        .name = "sink",
        .getter = {},
        .setter = propertySetter(),
        .documentation = ""
    }).isValid());

    Tokenizer tokens(&context, "test.nomad", "\"Nomad\".toUpper.sink = 10");
    auto statement = parser::parseLine(&context, nullptr, &tokens);

    BOOST_REQUIRE(statement != nullptr);
    BOOST_TEST(statement->resolve(&context, nullptr));
    BOOST_TEST(context.getErrorCount() == 0);
}

BOOST_AUTO_TEST_CASE(rejects_assignment_to_method) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    ScriptFunction function(CallableId::INVALID, "test", "test.nomad", "");
    function.registerVariable("name", runtime.getStringType());

    Tokenizer tokens(&context, "test.nomad", "name.toUpper = \"Other\"");
    auto statement = parser::parseLine(&context, &function, &tokens);

    BOOST_REQUIRE(statement != nullptr);
    BOOST_TEST(!statement->resolve(&context, &function));
    BOOST_REQUIRE(context.getFirstError() != nullptr);
    BOOST_TEST(context.getFirstError()->message.find("Cannot assign to native method") != NomadString::npos);
}

BOOST_AUTO_TEST_CASE(allows_object_method_on_temporary) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());

    BOOST_REQUIRE(runtime.addMethod(runtime.getTypeId("string"), {
        .name = "clear",
        .parameters = {},
        .returnTypeId = runtime.getTypeId("void"),
        .function = methodFunction(),
        .documentation = NomadDoc("Clear")
    }).isValid());

    Tokenizer tokens(&context, "test.nomad", "\"Nomad\".clear");
    auto statement = parser::parseLine(&context, nullptr, &tokens);

    BOOST_REQUIRE(statement != nullptr);
    BOOST_TEST(statement->resolve(&context, nullptr));
    BOOST_TEST(context.getErrorCount() == 0);
}

BOOST_AUTO_TEST_CASE(preserves_existing_qualified_identifier) {
    Runtime runtime;
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    ScriptFunction function(CallableId::INVALID, "test", "test.nomad", "");
    function.registerVariable("name", runtime.getStringType());
    function.registerVariable("name.length", runtime.getIntegerType());

    auto expression = parseExpression(context, &function, "name.length");

    BOOST_REQUIRE(expression != nullptr);
    BOOST_TEST(expression->resolve(&context, &function));
    BOOST_TEST(expression->getType() == runtime.getIntegerType());
}

BOOST_AUTO_TEST_CASE(context_assignment_takes_precedence_over_native_member_names) {
    Runtime runtime;
    runtime.registerVariableContext("entity", "this.", std::make_unique<SimpleVariableContext>());
    BOOST_REQUIRE(runtime.addProperty(runtime.getTypeId("string"), {
        .valueTypeId = runtime.getTypeId("int"),
        .name = "value",
        .getter = [](VirtualMachine*, RuntimeValue&) {},
        .setter = {},
        .documentation = ""
    }).isValid());
    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
    compiler->registerSources(&context, {{{"test", {"test.nomad",
        "this = 0\n"
        "this.value = 20\n"
        "this.value = this.value + 1\n"
        "this.length = 2\n"
        "return this.value + this.length\n"
    }}}, {}});

    const auto compiled = compiler->compileFunctions(&context);
    BOOST_REQUIRE_MESSAGE(compiled, (context.getFirstError() ? context.getFirstError()->message : "Compilation failed"));
    BOOST_TEST(runtime.getContextVariableId("this.value").isValid());
    BOOST_TEST(runtime.getContextVariableId("this.length").isValid());

    RuntimeValue result;
    runtime.executeFunction(runtime.getFunctionId("test"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 23);
}

BOOST_AUTO_TEST_CASE(rejects_invalid_indexer_syntax_and_types) {
    for (const auto* source : {
        "return \"text\"[]", "return \"text\"[0 1]", "return \"text\"[0",
        "return \"text\"[\"0\"]", "return \"text\"[1.0]", "return 1[0]",
        "\"text\"[0] = \"x\"", "value = \"text\"\nvalue[0] = \"x\""
    }) {
        BOOST_TEST_CONTEXT(source) {
            Runtime runtime;
            auto compiler = runtime.createCompiler();
            CompilerContext context(compiler.get());
            compiler->registerSources(&context, {{{"test", {"test.nomad", source}}}, {}});
            BOOST_TEST(!compiler->compileFunctions(&context));
            BOOST_REQUIRE(context.getFirstError() != nullptr);
            if (NomadString(source).find("= \"x\"") != NomadString::npos) {
                BOOST_TEST(context.getFirstError()->message.find("read-only") != NomadString::npos);
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
