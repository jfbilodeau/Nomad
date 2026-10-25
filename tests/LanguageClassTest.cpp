// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/compiler/ClassDeclaration.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Hub.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VariableContext.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <utility>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

bool prepareMembers(CompilerContext& context, Tokenizer& tokens, ScriptCallableBase* function) {
    tokens.reset();
    if (!tokens.nextLine()) return false;
    auto statement = tokens.next().textValue;
    if (statement == "open") tokens.next();
    const auto valid = preParseClassStatement(&context, function, &tokens, PreParsePhase::Signatures);
    tokens.reset();
    return valid && tokens.nextLine();
}

void registerCallbackTestFunctions(Runtime& runtime) {
    const auto* callbackType = runtime.getCallbackType({runtime.getIntegerType()}, runtime.getIntegerType());
    for (const auto* name : {"factory", "invoke"}) {
        BOOST_REQUIRE(runtime.registerNativeFunction(
            name, [](VirtualMachine*) {}, {defParameter("callback", callbackType, "")},
            runtime.getIntegerType(), ""
        ).isValid());
    }
}

} // namespace

BOOST_AUTO_TEST_SUITE(language_classes)

BOOST_AUTO_TEST_CASE(init_is_contextual_not_a_globally_reserved_identifier)
{
    const Runtime runtime;
    BOOST_TEST(runtime.getKeywordId("init").isInvalid());
    BOOST_TEST(runSource("init = 42\nreturn init").integerValue == 42);
    BOOST_TEST(runSource("fun init value:int\nreturn value\nend\nreturn init 42").integerValue == 42);
    BOOST_TEST(runSource("hub example\ninit = 42\nend\nreturn example.init").integerValue == 42);
    BOOST_TEST(!compileErrors("class Point\nfun init\nend\nend").empty());
    BOOST_TEST(!compileErrors("class Point\ninit = 42\nend").empty());
}

BOOST_AUTO_TEST_CASE(empty_declarations_publish_closed_and_open_object_types)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "class Closed\n# Empty body.\nend\n"
        "open class Opened\n\nend\nreturn 42"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto* closed = runtime.getTypeByName("Closed")->asObjectType();
    const auto* opened = runtime.getTypeByName("Opened")->asObjectType();
    BOOST_REQUIRE(closed != nullptr);
    BOOST_REQUIRE(opened != nullptr);
    BOOST_TEST(!closed->isOpen());
    BOOST_TEST(opened->isOpen());
    BOOST_TEST(closed->getId() == runtime.getTypeId("Closed"));
    BOOST_TEST(closed->getConstructors().empty());
    BOOST_TEST(opened->getMethods().empty());
    BOOST_TEST(closed->getProperties().empty());
    BOOST_TEST(fixture.execute("main").integerValue == 42);

    auto* compiler = &fixture.getCompiler();
    CompilerContext context(compiler);
    Tokenizer tokens(&context, "ast.nomad", "class Closed\nend");
    BOOST_REQUIRE(tokens.nextLine());
    const auto statement = parser::parseLine(&context, runtime.getScriptFunction(runtime.getFunctionId("main")), &tokens);
    const auto* declaration = dynamic_cast<const ClassDeclaration*>(statement.get());
    BOOST_REQUIRE(declaration != nullptr);
    BOOST_TEST(declaration->isDeclaration());
    BOOST_TEST(declaration->getType() == closed);
    BOOST_TEST(!context.hasError());
}

BOOST_AUTO_TEST_CASE(class_symbols_are_available_before_cross_file_signatures)
{
    for (const auto reversed : {false, true}) {
        LanguageTestFixture fixture;
        fixture.addFunction(reversed ? "z" : "a",
            "fun identity value:model.Point\nreturn value\nend"
        );
        fixture.addFunction(reversed ? "a" : "z",
            "hub model\nclass Point\nend\nend"
        );
        fixture.addFunction("main", "return 42");
        BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
        auto& runtime = fixture.getRuntime();
        const auto* type = runtime.getTypeByName("model.Point");
        BOOST_REQUIRE(type != nullptr);
        const auto* identity = runtime.getScriptFunction(runtime.getFunctionId("identity"));
        BOOST_REQUIRE(identity != nullptr);
        BOOST_TEST(identity->getParameterType(0) == type);
        BOOST_TEST(identity->getReturnType() == type);
        const auto* hub = runtime.getHub(runtime.getHubId("model"));
        BOOST_REQUIRE(hub != nullptr);
        BOOST_REQUIRE(hub->getMember("Point") != nullptr);
        BOOST_TEST(std::get<const Type*>(*hub->getMember("Point")) == type);
    }
}

BOOST_AUTO_TEST_CASE(nested_hubs_prefix_class_names_without_implicit_type_lookup)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "hub game\nhub model\nopen class Point\nend\n"
        "fun identity value:game.model.Point\nreturn value\nend\nend\nend"
    );
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* type = fixture.getRuntime().getTypeByName("game.model.Point")->asObjectType();
    BOOST_REQUIRE(type != nullptr);
    BOOST_TEST(type->isOpen());
    BOOST_TEST(fixture.getRuntime().getTypeByName("Point") == nullptr);
    BOOST_TEST(!compileErrors("hub game\nclass Point\nend\nfun identity value:Point\nreturn value\nend\nend").empty());
}

BOOST_AUTO_TEST_CASE(duplicate_classes_are_rejected_even_when_open)
{
    for (const auto* first : {"class Point", "open class Point"}) {
        for (const auto* second : {"class Point", "open class Point"}) {
            LanguageTestFixture fixture;
            fixture.addFunction("a", NomadString(first) + "\nend");
            fixture.addFunction("b", NomadString(second) + "\nend");
            BOOST_TEST(!fixture.compile());
            BOOST_TEST(fixture.hasError("Cannot declare class 'Point'"));
        }
    }
    LanguageTestFixture fixture;
    BOOST_REQUIRE(fixture.getRuntime().createObjectType("Point", ObjectTypeMode::Closed).isValid());
    fixture.addFunction("main", "class Point\nend");
    BOOST_TEST(!fixture.compile());
}

BOOST_AUTO_TEST_CASE(invalid_class_definitions_report_source_errors)
{
    for (const auto* source : {
        "class", "class Point", "class 123\nend", "class Point trailing\nend",
        "class open\nend", "class int\nend", "class Point\nend trailing",
        "class Point\nx = 1\nend", "class Point\ninit\nend\nend",
        "class Point\nfun move\nend\nend", "class Point\nclass Nested\nend\nend",
        "fun inner\nclass Point\nend\nend", "if true\nopen class Point\nend\nend",
        "hub Point\nend\nclass Point\nend", "class Point\nend\nhub Point\nend",
        "const Point = 1\nclass Point\nend", "class Point\nend\nconst Point = 1",
        "fun Point\nend\nclass Point\nend", "class Point\nend\nfun Point\nend",
        "closed class Point\nend", "class Point\nend\nvalue = new Point"
    }) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!compileErrors(source).empty());
        }
    }
    LanguageTestFixture fixture;
    fixture.addFunction("main", "class Point\nvalue = 1\nend");
    BOOST_TEST(!fixture.compile());
    const auto* diagnostic = fixture.findError("Class member execution is not supported yet");
    BOOST_REQUIRE(diagnostic != nullptr);
    BOOST_TEST(diagnostic->sourceName == "main.nomad");
    BOOST_TEST(diagnostic->line == 2u);
    BOOST_TEST(fixture.getRuntime().getTypeByName("Point") != nullptr);
}

BOOST_AUTO_TEST_CASE(member_declarations_retain_typed_headers_initializers_and_bodies)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "class Point\nend");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    auto* compiler = &fixture.getCompiler();
    CompilerContext context(compiler);
    std::unique_ptr<Statement> statement;
    {
        Tokenizer tokens(&context, "members.nomad",
            "class Point\n"
            "    name = \"point\"\n"
            "    scores = 1 + 10\n"
            "    init\n"
            "        x = 0.0\n"
            "    end\n"
            "    init x:float y:float\n"
            "        copyX = x\n"
            "        copyY = y\n"
            "    end\n"
            "    fun move target:Point\n"
            "        copy = target\n"
            "    end\n"
            "end\n"
            "return 42"
        );
        BOOST_REQUIRE(prepareMembers(context, tokens, runtime.getScriptCallable(runtime.getFunctionId("main"))));
        statement = parser::parseLine(&context, runtime.getScriptFunction(runtime.getFunctionId("main")), &tokens);
        BOOST_REQUIRE_MESSAGE(statement != nullptr,
            (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Missing class AST"));
        BOOST_TEST(!context.hasError());
        BOOST_TEST(tokens.getLineIndex() == 14u);
        BOOST_REQUIRE(tokens.nextLine());
        BOOST_TEST(tokens.is("return"));
    }
    const auto* declaration = dynamic_cast<const ClassDeclaration*>(statement.get());
    BOOST_REQUIRE(declaration != nullptr);
    const auto& members = declaration->getMembers();
    BOOST_REQUIRE(members.size() == 5u);
    BOOST_TEST((members[0]->getKind() == ClassMemberKind::Field));
    BOOST_TEST(members[0]->getName() == "name");
    BOOST_TEST(members[0]->getLine() == 2u);
    BOOST_TEST(members[0]->getColumn() == 5u);
    const auto& initializer = members[0]->getSourceLines();
    BOOST_REQUIRE(initializer.size() == 1u);
    BOOST_TEST(initializer[0].line == 2u);
    BOOST_REQUIRE(initializer[0].tokens.size() == 1u);
    BOOST_TEST((initializer[0].tokens[0].type == TokenType::String));
    BOOST_TEST(initializer[0].tokens[0].textValue == "point");
    BOOST_TEST(initializer[0].tokens[0].column == 12u);
    BOOST_REQUIRE(members[1]->getSourceLines()[0].tokens.size() == 3u);
    BOOST_TEST(members[1]->getSourceLines()[0].tokens[2].integerValue == 10);
    BOOST_TEST((members[2]->getKind() == ClassMemberKind::Constructor));
    BOOST_TEST(members[2]->getParameters().empty());
    BOOST_REQUIRE(members[2]->getSourceLines().size() == 1u);
    BOOST_TEST(members[2]->getSourceLines()[0].tokens[2].floatValue == 0.0f);
    BOOST_TEST(members[2]->getEndLine() == 6u);
    BOOST_REQUIRE(members[3]->getParameters().size() == 2u);
    BOOST_TEST(members[3]->getParameters()[0].name == "x");
    BOOST_TEST(members[3]->getParameters()[1].name == "y");
    BOOST_TEST(members[3]->getParameters()[0].type == runtime.getFloatType());
    BOOST_TEST((members[4]->getKind() == ClassMemberKind::Method));
    BOOST_TEST(members[4]->getName() == "move");
    BOOST_REQUIRE(members[4]->getParameters().size() == 1u);
    BOOST_TEST(members[4]->getParameters()[0].type == declaration->getType());
    BOOST_TEST(declaration->getType()->getConstructors().size() == 2u);
    BOOST_TEST(declaration->getType()->getMethods().size() == 2u);
    BOOST_REQUIRE(members[0]->getInitializer() != nullptr);
    BOOST_TEST(members[2]->getCallableId().isValid());
    BOOST_TEST(declaration->getType()->getProperties().empty());
    BOOST_TEST(runtime.getFunctionId("move").isInvalid());
    BOOST_TEST(runtime.getFunctionId("init").isInvalid());
}

BOOST_AUTO_TEST_CASE(member_source_scanning_balances_blocks_callbacks_and_loop_closers)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "open class Point\nend");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    registerCallbackTestFunctions(runtime);
    auto* compiler = &fixture.getCompiler();
    CompilerContext context(compiler);
    Tokenizer tokens(&context, "blocks.nomad",
        "open class Point\n"
        "callback = factory fun value\nreturn value\nend\n"
        "fun process\n"
        "\n"
        "while true\n"
        "if true break\n"
        "if false continue\n"
        "if true\nvalue = \"end\"\nelse if false\nvalue = \"fun\"\nend\n"
        "end\n"
        "do\nvalue = 1\nend until true\n"
        "for index in range 1 2\nvalue = index\nend\n"
        "invoke fun item\nif true\nreturn item\nend\nend\n"
        "invoke then item\nreturn item\n"
        "end\n"
        "last = 42\n"
        "end\n"
        "return 7"
    );
    BOOST_REQUIRE(prepareMembers(context, tokens, runtime.getScriptCallable(runtime.getFunctionId("main"))));
    const auto statement = parser::parseLine(&context, runtime.getScriptFunction(runtime.getFunctionId("main")), &tokens);
    const auto* declaration = dynamic_cast<const ClassDeclaration*>(statement.get());
    BOOST_REQUIRE_MESSAGE(declaration != nullptr,
        (context.getFirstError() ? formatDiagnostic(*context.getFirstError()) : "Missing class AST"));
    BOOST_TEST(!context.hasError());
    const auto& members = declaration->getMembers();
    BOOST_REQUIRE(members.size() == 3u);
    BOOST_REQUIRE(members[0]->getSourceLines().size() == 3u);
    BOOST_TEST(members[0]->getSourceLines().back().tokens[0].textValue == "end");
    BOOST_TEST(members[1]->getSourceLines().front().line == 7u);
    BOOST_TEST(members[1]->getSourceLines().front().tokens[0].textValue == "while");
    BOOST_TEST(members[1]->getSourceLines().back().tokens[0].textValue == "return");
    BOOST_TEST(members[2]->getName() == "last");
    BOOST_REQUIRE(tokens.nextLine());
    BOOST_TEST(tokens.is("return"));
}

BOOST_AUTO_TEST_CASE(member_overloads_register_distinct_parameter_types_on_the_class)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "class Point\nend");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    auto* compiler = &fixture.getCompiler();
    CompilerContext context(compiler);
    Tokenizer tokens(&context, "overloads.nomad",
        "class Point\n"
        "init value:int\nend\ninit value:float\nend\n"
        "fun move value:int\nend\nfun move value:float\nend\n"
        "end"
    );
    BOOST_REQUIRE(prepareMembers(context, tokens, runtime.getScriptCallable(runtime.getFunctionId("main"))));
    const auto statement = parser::parseLine(&context, runtime.getScriptFunction(runtime.getFunctionId("main")), &tokens);
    const auto* declaration = dynamic_cast<const ClassDeclaration*>(statement.get());
    BOOST_REQUIRE(declaration != nullptr);
    BOOST_TEST(!context.hasError());
    const auto& members = declaration->getMembers();
    BOOST_REQUIRE(members.size() == 4u);
    for (auto index = NomadIndex{0}; index < members.size(); ++index) {
        BOOST_REQUIRE(members[index]->getParameters().size() == 1u);
        BOOST_TEST(members[index]->getParameters()[0].type ==
            (index % 2 == 0 ? runtime.getIntegerType() : runtime.getFloatType()));
        BOOST_TEST(members[index]->getSourceLines().empty());
    }
    BOOST_TEST(declaration->getType()->getConstructors().size() == 2u);
    BOOST_TEST(declaration->getType()->getMethods().size() == 2u);
}

BOOST_AUTO_TEST_CASE(class_parameter_types_are_resolved_after_all_class_symbols_are_published)
{
    for (const auto reversed : {false, true}) {
        LanguageTestFixture fixture;
        fixture.addFunction(reversed ? "z" : "a",
            "hub model\nclass First\ninit value:model.Second\nend\n"
            "fun accept value:model.Second\nend\nend\nend"
        );
        fixture.addFunction(reversed ? "a" : "z", "hub other\nclass Second\nend\nend\nhub model.Second\nend");
        // A dotted namespace does not provide a type for the signature.
        BOOST_TEST(!fixture.compile());
        BOOST_TEST(fixture.hasError("Unknown type 'model.Second'"));

        LanguageTestFixture validTypes;
        validTypes.addFunction(reversed ? "z" : "a",
            "open hub model\nclass First\ninit value:model.Second\nend\n"
            "fun accept value:model.Second\nend\nend\nend"
        );
        validTypes.addFunction(reversed ? "a" : "z", "open hub model\nclass Second\nend\nend");
        BOOST_TEST(!validTypes.compile());
        BOOST_TEST(validTypes.hasError("Class member execution is not supported yet"));
        BOOST_TEST(!validTypes.hasError("Unknown type"));
        BOOST_TEST(validTypes.getRuntime().getFunctionId("model.accept").isInvalid());
    }
}

BOOST_AUTO_TEST_CASE(invalid_member_headers_duplicates_and_unclosed_bodies_are_rejected)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "class Point\nend");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    for (const auto* body : {
        "value =", "value", "other.value = 1", "fun open\nend", "fun other.move\nend",
        "\"init\"\nend", "\"fun\" move\nend", "\"field\" = 1",
        "fun move value\nend", "init value:Missing\nend", "init value:int value:int\nend",
        "fun move open:int\nend", "value = 1\nvalue = 2",
        "fun move x:int\nend\nfun move y:int\nend",
        "init x:int\nend\ninit y:int\nend",
        "value = 1\nfun value\nend", "fun value\nend\nvalue = 1",
        "fun move\nif true\nend", "callback = factory fun item", "init\nend trailing"
    }) {
        BOOST_TEST_CONTEXT(body) {
            auto compiler = runtime.createCompiler();
            CompilerContext context(compiler.get());
            Tokenizer tokens(&context, "invalid-members.nomad", NomadString("class Point\n") + body + "\nend");
            BOOST_REQUIRE(tokens.nextLine());
            const auto statement = parser::parseLine(
                &context, runtime.getScriptFunction(runtime.getFunctionId("main")), &tokens
            );
            BOOST_TEST(statement == nullptr);
            BOOST_TEST(context.hasError());
        }
    }
    BOOST_TEST(!compileErrors("hub model\nclass Point\nend\nclass Other\ninit value:Point\nend\nend\nend").empty());
}

BOOST_AUTO_TEST_CASE(class_declarations_are_not_supported_by_the_interpreter)
{
    for (const auto* source : {"class Point\nend", "open class Point\nend"}) {
        Runtime runtime;
        auto machine = runtime.createVirtualMachine();
        InterpreterContext context(*machine);
        BOOST_TEST(!Interpreter{}.execute(source, context));
        BOOST_TEST(context.hasError());
        BOOST_TEST(runtime.getTypeByName("Point") == nullptr);
    }
}

BOOST_AUTO_TEST_CASE(member_signatures_precede_body_parsing_and_receiver_types_are_inferred)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "class Point\n"
        "value = 1.0\n"
        "init value:float\ncopy = value\nreturn\nend\n"
        "fun identity\nreturn this\nend\n"
        "fun echo value:int\ncopy = value\nreturn copy\nend\n"
        "end"
    );
    auto sawSignatures = false;
    fixture.getCompiler().registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
        if (phase != CompilationPhase::Parse) return;
        const auto* type = fixture.getRuntime().getTypeByName("Point")->asObjectType();
        BOOST_REQUIRE(type != nullptr);
        BOOST_TEST(type->getConstructors().size() == 1u);
        BOOST_TEST(type->getMethods().size() == 3u);
        sawSignatures = true;
    });
    BOOST_TEST(!fixture.compile());
    BOOST_TEST(sawSignatures);
    BOOST_TEST(fixture.hasError("Class member execution is not supported yet"));
    BOOST_TEST(!fixture.hasError("Missing `return`"));
    BOOST_TEST(!fixture.hasError("Unknown identifier"));
    auto& runtime = fixture.getRuntime();
    const auto* type = runtime.getTypeByName("Point")->asObjectType();
    for (const auto* method : type->getMethods()) {
        const auto* callable = runtime.getScriptCallable(method->getId());
        BOOST_REQUIRE(callable != nullptr);
        BOOST_TEST(callable->getVariableType(callable->getVariableId("this")) == type);
        BOOST_TEST(callable->getParameterId("this") == NOMAD_INVALID_ID);
        BOOST_TEST(runtime.getScriptFunction(method->getId()) == nullptr);
        if (method->getName() == "identity") BOOST_TEST(method->getReturnType() == type);
        if (method->getName() == "echo") BOOST_TEST(method->getReturnType() == runtime.getIntegerType());
    }
    BOOST_TEST(runtime.getFunctionId("echo").isInvalid());
    BOOST_TEST(runtime.getFunctionId("Point.echo").isInvalid());
}

BOOST_AUTO_TEST_CASE(member_bodies_and_initializers_report_ordinary_source_errors)
{
    for (const auto* source : {
        "class Point\nvalue = 1 +\nend",
        "class Point\nfun move\nreturn 1 +\nend\nend",
        "class Point\nfun move\nreturn missing\nend\nend",
        "class Point\nfun move\nif true\nreturn 1\nend\nend\nend",
        "class Point\ninit\nreturn 1\nend\nend",
        "class Point\nfun move this:int\nend\nend",
        "class Point\nfun move\nparams this:Point\nend\nend",
        "class Point\nfun move\nthis = 1\nend\nend",
        "class Point\nfun move\nthis.value = 1\nend\nend",
        "class Point\nfun move\nreturn this.value\nend\nend",
        "class Point\nfun move\nthis.move\nend\nend",
        "class Point\nfun move\nreturn this.move()\nend\nend"
    }) {
        BOOST_TEST_CONTEXT(source) {
            LanguageTestFixture fixture;
            fixture.addFunction("main", source);
            BOOST_TEST(!fixture.compile());
            BOOST_TEST(fixture.getErrorCount() > 0u);
            BOOST_TEST(!fixture.hasError("Internal error"));
        }
    }
    LanguageTestFixture fixture;
    fixture.addFunction("main", "class Point\nfun move\nreturn missing\nend\nend");
    BOOST_TEST(!fixture.compile());
    const auto* diagnostic = fixture.findError("Unknown identifier: missing");
    BOOST_REQUIRE(diagnostic != nullptr);
    BOOST_TEST(diagnostic->sourceName == "main.nomad");
    BOOST_TEST(diagnostic->line == 3u);
}

BOOST_AUTO_TEST_CASE(bare_member_names_remain_locals_and_initializers_resolve_normally)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "class Point\nvalue = 1.0\nfun move\nvalue = 2\nreturn value\nend\nend"
    );
    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.hasError("Class member execution is not supported yet"));
    const auto* type = fixture.getRuntime().getTypeByName("Point")->asObjectType();
    for (const auto* method : type->getMethods()) {
        if (method->getName() == "move") {
            const auto* callable = fixture.getRuntime().getScriptCallable(method->getId());
            BOOST_TEST(method->getReturnType() == fixture.getRuntime().getIntegerType());
            BOOST_TEST(callable->getVariableType(callable->getVariableId("value")) ==
                fixture.getRuntime().getIntegerType());
        }
    }
    LanguageTestFixture invalidInitializer;
    invalidInitializer.addFunction("main", "class Point\nvalue = missing\nend");
    BOOST_TEST(!invalidInitializer.compile());
    BOOST_TEST(invalidInitializer.hasError("Unknown identifier: missing"));
}

BOOST_AUTO_TEST_CASE(class_fields_are_discovered_across_branches_and_constructor_overloads)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "class Point\n"
        "name = \"point\"\n"
        "fun read\nreturn this.x\nend\n"
        "init enabled:bool\n"
        "if enabled\nthis.x = 1.0\nelse\nthis.y = 2\nend\n"
        "end\n"
        "init value:float\nthis.x = value\nthis.label = this.name\nend\n"
        "fun update value:float\nthis.x = value\nx = 42\nreturn x\nend\n"
        "end"
    );
    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.hasError("Class member execution is not supported yet"));
    BOOST_TEST(fixture.getErrorCount() == 1u);
    auto& runtime = fixture.getRuntime();
    const auto* type = runtime.getTypeByName("Point")->asObjectType();
    BOOST_REQUIRE(type != nullptr);
    BOOST_REQUIRE(type->getProperties().size() == 4u);
    for (const auto* name : {"name", "x", "y", "label"}) {
        BOOST_REQUIRE(type->getProperty(name) != nullptr);
        BOOST_TEST(runtime.getProperty(type->getProperty(name)->getId()) == type->getProperty(name));
    }
    BOOST_TEST(type->getProperty("x")->getValueType() == runtime.getFloatType());
    BOOST_TEST(type->getProperty("y")->getValueType() == runtime.getIntegerType());
    BOOST_TEST(type->getProperty("label")->getValueType() == runtime.getStringType());
    for (const auto* method : type->getMethods()) {
        if (method->getName() == "read") {
            BOOST_TEST(method->getReturnType() == runtime.getFloatType());
        }
        if (method->getName() == "update") {
            BOOST_TEST(method->getReturnType() == runtime.getIntegerType());
            const auto* callable = runtime.getScriptCallable(method->getId());
            BOOST_TEST(callable->getVariableId("this.x").isInvalid());
        }
    }
}

BOOST_AUTO_TEST_CASE(field_inference_retries_forward_dependencies_until_stable)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "class Chain\n"
        "first = this.second\nsecond = this.third\nthird = this.fourth\n"
        "fourth = this.fifth\nfifth = this.sixth\nsixth = 42\n"
        "init\nthis.seventh = this.first\nend\n"
        "end"
    );
    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.getErrorCount() == 1u);
    BOOST_TEST(fixture.hasError("Class member execution is not supported yet"));
    const auto* type = fixture.getRuntime().getTypeByName("Chain")->asObjectType();
    BOOST_REQUIRE(type->getProperties().size() == 7u);
    for (const auto* property : type->getProperties()) {
        BOOST_TEST(property->getValueType() == fixture.getRuntime().getIntegerType());
    }
}

BOOST_AUTO_TEST_CASE(class_fields_report_conflicts_unknown_reads_and_method_only_declarations)
{
    for (const auto* source : {
        "class Point\nvalue = 1\ninit\nthis.value = \"wrong\"\nend\nend",
        "class Point\ninit enabled:bool\nif enabled\nthis.x = 1\nelse\nthis.x = 1.0\nend\nend\nend",
        "class Point\ninit value:int\nthis.x = value\nend\ninit value:float\nthis.x = value\nend\nend"
    }) {
        LanguageTestFixture fixture;
        fixture.addFunction("main", source);
        BOOST_TEST(!fixture.compile());
        BOOST_TEST(fixture.hasError("Cannot assign type"));
    }
    for (const auto* source : {
        "class Point\nfun x\nend\ninit\nthis.x = 1\nend\nend",
        "class Point\ninit\nthis.x = 1\nend\nfun x\nend\nend"
    }) {
        LanguageTestFixture fixture;
        fixture.addFunction("main", source);
        BOOST_TEST(!fixture.compile());
        BOOST_TEST(fixture.hasError("conflicts with a method"));
    }
    for (const auto* source : {
        "class Point\nfun read\nreturn this.missing\nend\nend",
        "open class Point\nfun write\nthis.missing = 1\nend\nend",
        "class Point\nfirst = this.second\nsecond = this.first\nend"
    }) {
        LanguageTestFixture fixture;
        fixture.addFunction("main", source);
        BOOST_TEST(!fixture.compile());
        BOOST_TEST(fixture.hasError("Unknown class field"));
    }
}

BOOST_AUTO_TEST_CASE(field_slots_are_owner_local_stable_and_distinct_from_catalog_ids)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "class First\nx = 1\ny = \"two\"\nend\n"
        "class Second\nx = true\nend"
    );
    BOOST_TEST(!fixture.compile());
    auto& runtime = fixture.getRuntime();
    const auto* first = runtime.getTypeByName("First")->asObjectType();
    const auto* second = runtime.getTypeByName("Second")->asObjectType();
    BOOST_REQUIRE(first->getProperty("x") != nullptr);
    BOOST_REQUIRE(first->getProperty("y") != nullptr);
    BOOST_REQUIRE(second->getProperty("x") != nullptr);
    const auto firstId = first->getProperty("x")->getId();
    const auto secondId = second->getProperty("x")->getId();
    BOOST_TEST(firstId != secondId);
    BOOST_TEST(first->getFieldIndex(firstId) == 0u);
    BOOST_TEST(second->getFieldIndex(secondId) == 0u);
    BOOST_TEST(first->getFieldIndex(first->getProperty("y")->getId()) == 1u);
    BOOST_TEST(first->getFieldIndex(secondId) == NOMAD_INVALID_INDEX);
    BOOST_TEST(first->getFieldIndex(PropertyId{}) == NOMAD_INVALID_INDEX);
    BOOST_TEST(runtime.registerProperty(first, "x", runtime.getIntegerType()) == firstId);
    BOOST_REQUIRE(runtime.registerProperty(first, "later", runtime.getBooleanType()).isValid());
    BOOST_TEST(first->getFieldIndex(firstId) == 0u);
    BOOST_TEST(first->getFieldIndex(first->getProperty("later")->getId()) == 2u);
    const auto nativeId = runtime.addProperty(first->getId(), {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .name = "nativeValue",
        .getter = [](VirtualMachine*, RuntimeValue& value) { value = RuntimeValue{NomadInteger{1}}; },
        .setter = {},
        .documentation = ""
    });
    BOOST_REQUIRE(nativeId.isValid());
    BOOST_TEST(first->getFieldIndex(nativeId) == NOMAD_INVALID_INDEX);
    const auto lastId = runtime.registerProperty(first, "last", runtime.getBooleanType());
    BOOST_REQUIRE(lastId.isValid());
    BOOST_TEST(first->getFieldIndex(lastId) == 3u);
}

BOOST_AUTO_TEST_CASE(explicit_field_declarations_link_to_property_metadata)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "class Point\nend");
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    auto* function = runtime.getScriptCallable(runtime.getFunctionId("main"));
    CompilerContext context(&fixture.getCompiler());
    Tokenizer tokens(&context, "fields.nomad", "class Point\nx = 42\nend");
    BOOST_REQUIRE(prepareMembers(context, tokens, function));
    const auto statement = parser::parseLine(&context, function, &tokens);
    const auto* declaration = dynamic_cast<const ClassDeclaration*>(statement.get());
    BOOST_REQUIRE(declaration != nullptr);
    BOOST_TEST(!statement->resolve(&context, function));
    const auto* property = declaration->getType()->getProperty("x");
    BOOST_REQUIRE(property != nullptr);
    BOOST_TEST(declaration->getMembers()[0]->getPropertyId() == property->getId());
    BOOST_TEST(property->getValueType() == runtime.getIntegerType());
}

BOOST_AUTO_TEST_CASE(field_reads_and_writes_capture_this_without_native_member_name_interference)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    registerCallbackTestFunctions(runtime);
    const auto otherId = runtime.createObjectType("Other", ObjectTypeMode::Closed);
    BOOST_REQUIRE(otherId.isValid());
    BOOST_REQUIRE(runtime.addProperty(otherId, {
        .valueTypeId = runtime.getFloatType()->getId(),
        .name = "value",
        .getter = [](VirtualMachine*, RuntimeValue& value) { value = RuntimeValue{NomadFloat{1.0}}; },
        .setter = {},
        .documentation = ""
    }).isValid());
    fixture.addFunction("main",
        "class Point\nvalue = 1\n"
        "fun callback\nresult = factory fun item\nthis.value = item\nreturn this.value\nend\nreturn result\nend\n"
        "fun describe\nreturn $\"value {this.value}\"\nend\nend"
    );
    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.getErrorCount() == 1u);
    BOOST_TEST(fixture.hasError("Class member execution is not supported yet"));
    const auto* type = runtime.getTypeByName("Point")->asObjectType();
    BOOST_REQUIRE(type->getProperty("value") != nullptr);
    BOOST_TEST(type->getProperty("value")->getValueType() == runtime.getIntegerType());
    auto foundCapture = false;
    std::vector<ScriptFunction*> functions;
    runtime.getFunctions(functions);
    for (const auto* function : functions) {
        if (function->getCaptureId("this") != NOMAD_INVALID_ID) {
            BOOST_TEST(function->getCaptureType(function->getCaptureId("this")) == type);
            BOOST_TEST(function->getVariableId("this.value").isInvalid());
            foundCapture = true;
        }
    }
    BOOST_TEST(foundCapture);
}

BOOST_AUTO_TEST_CASE(member_callbacks_capture_this_and_format_placeholders_use_member_scope)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "class Point\n"
        "fun describe value:int\nreturn $\"value {value}\"\nend\n"
        "fun callback\nresult = receiverFactory fun\nreturn this\nend\nreturn result\nend\n"
        "end"
    );
    fixture.getCompiler().registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
        if (phase != CompilationPhase::Parse) return;
        auto& runtime = fixture.getRuntime();
        const auto* type = runtime.getTypeByName("Point");
        const auto* callback = runtime.getCallbackType({}, type);
        BOOST_REQUIRE(runtime.registerNativeFunction(
            "receiverFactory", [](VirtualMachine*) {}, {defParameter("callback", callback, "")}, type, ""
        ).isValid());
    });
    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.hasError("Class member execution is not supported yet"));
    BOOST_TEST(!fixture.hasError("Unknown identifier"));
    BOOST_TEST(!fixture.hasError("Callback return type"));
    auto& runtime = fixture.getRuntime();
    const auto* type = runtime.getTypeByName("Point")->asObjectType();
    CallableId callbackMethod;
    for (const auto* method : type->getMethods()) {
        if (method->getName() == "describe") BOOST_TEST(method->getReturnType() == runtime.getStringType());
        if (method->getName() == "callback") {
            BOOST_TEST(method->getReturnType() == type);
            callbackMethod = method->getId();
        }
    }
    auto foundCapture = false;
    std::vector<ScriptFunction*> functions;
    runtime.getFunctions(functions);
    for (const auto* function : functions) {
        if (function->getParentId() == callbackMethod) {
            BOOST_TEST(function->getCaptureId("this") != NOMAD_INVALID_ID);
            BOOST_TEST(function->getCaptureType(function->getCaptureId("this")) == type);
            foundCapture = true;
        }
    }
    BOOST_TEST(foundCapture);
}

BOOST_AUTO_TEST_CASE(member_bodies_use_registered_declaration_pre_parsers)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "class Point\nfun calculate\nconst answer = 42\nreturn answer\nend\nend"
    );
    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.hasError("Class member execution is not supported yet"));
    BOOST_TEST(!fixture.hasError("Unknown identifier"));
    const auto* type = fixture.getRuntime().getTypeByName("Point")->asObjectType();
    BOOST_REQUIRE(type->getMethods().size() == 1u);
    BOOST_TEST(type->getMethods()[0]->getReturnType() == fixture.getRuntime().getIntegerType());
}

BOOST_AUTO_TEST_CASE(class_receiver_never_falls_back_to_entity_context_variables)
{
    for (const auto* body : {"return this.value", "this.value = 2"}) {
        LanguageTestFixture fixture;
        auto& runtime = fixture.getRuntime();
        const auto contextId = runtime.registerVariableContext(
            "entity", "this.", std::make_unique<SimpleVariableContext>()
        );
        BOOST_REQUIRE(contextId.isValid());
        const auto variableId = runtime.registerContextVariable(contextId, "this.value", runtime.getIntegerType());
        BOOST_REQUIRE(variableId.isValid());
        fixture.addFunction("main", NomadString("class Point\nfun move\n") + body + "\nend\nend");
        BOOST_TEST(!fixture.compile());
        BOOST_TEST(fixture.hasError("Unknown class field 'value'"));
        const auto localId = runtime.getVariableContext(contextId)->getFirstVariableId();
        BOOST_TEST(!runtime.getVariableContext(contextId)->isRead(localId));
        BOOST_TEST(!runtime.getVariableContext(contextId)->isWritten(localId));
    }
}

BOOST_AUTO_TEST_CASE(external_open_class_fields_are_shared_across_functions_and_aliases)
{
    for (const auto reversed : {false, true}) {
        LanguageTestFixture fixture;
        fixture.addFunction("types", "open class Point\nend");
        fixture.addFunction(reversed ? "a" : "z",
            "fun write point:Point enabled:bool\nalias = point\nif enabled\nalias.extra = 42\nend\nend"
        );
        fixture.addFunction(reversed ? "z" : "a",
            "fun read point:Point\nreturn point.extra\nend"
        );
        CompilerContext context(&fixture.getCompiler());
        BOOST_TEST(fixture.getCompiler().compileScripts(&context));
        BOOST_TEST(context.getWarningCount() == 0u);
        const auto* type = fixture.getRuntime().getTypeByName("Point")->asObjectType();
        BOOST_REQUIRE(type->getProperty("extra") != nullptr);
        BOOST_TEST(type->getProperty("extra")->getValueType() == fixture.getRuntime().getIntegerType());
        const auto* read = fixture.getRuntime().getScriptFunction(fixture.getRuntime().getFunctionId("read"));
        BOOST_REQUIRE(read != nullptr);
        BOOST_TEST(read->getReturnType() == fixture.getRuntime().getIntegerType());
        const auto* write = fixture.getRuntime().getScriptFunction(fixture.getRuntime().getFunctionId("write"));
        BOOST_REQUIRE(write != nullptr);
        BOOST_TEST(write->getVariableId("alias.extra").isInvalid());
        BOOST_TEST(context.getErrorCount() == 0u);
    }
}

BOOST_AUTO_TEST_CASE(unread_external_fields_warn_once_at_the_introducing_assignment)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "open class Point\nend\n"
        "fun write point:Point\npoint.extra = 1\npoint.extra = 2\nend"
    );
    CompilerContext context(&fixture.getCompiler());
    BOOST_TEST(fixture.getCompiler().compileScripts(&context));
    BOOST_TEST(context.getWarningCount() == 1u);
    const Diagnostic* warning = nullptr;
    for (const auto& diagnostic : context.getDiagnostics()) {
        if (diagnostic.severity == DiagnosticSeverity::Warning) warning = &diagnostic;
    }
    BOOST_REQUIRE(warning != nullptr);
    BOOST_TEST(warning->message == "Externally introduced field 'Point.extra' is assigned but never read");
    BOOST_TEST(warning->sourceName == "main.nomad");
    BOOST_TEST(warning->line == 4u);
    BOOST_TEST(warning->column == 1u);
}

BOOST_AUTO_TEST_CASE(external_reads_of_never_declared_fields_are_errors_not_declarations)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main",
        "open class Point\nend\nfun read point:Point\nreturn point.missing\nend"
    );
    BOOST_TEST(!fixture.compile());
    const auto* error = fixture.findError("Unknown class field 'missing'");
    BOOST_REQUIRE(error != nullptr);
    BOOST_TEST(error->sourceName == "main.nomad");
    BOOST_TEST(error->line == 4u);
    BOOST_TEST(fixture.getRuntime().getTypeByName("Point")->asObjectType()->getProperties().empty());
}

BOOST_AUTO_TEST_CASE(external_class_fields_reject_closed_types_conflicting_types_and_methods)
{
    for (const auto* source : {
        "class Point\nend\nfun write point:Point\npoint.extra = 1\nend",
        "open class Point\nend\nfun write point:Point\npoint.extra = 1\npoint.extra = \"wrong\"\nend",
        "open class Point\nfun extra\nend\nend\nfun write point:Point\npoint.extra = 1\nend"
    }) {
        LanguageTestFixture fixture;
        fixture.addFunction("main", source);
        BOOST_TEST(!fixture.compile());
        BOOST_TEST((fixture.hasError("Unknown class field") || fixture.hasError("Cannot assign type") ||
            fixture.hasError("conflicts with a method")));
    }
}

BOOST_AUTO_TEST_CASE(internal_declarations_and_reads_do_not_produce_external_unused_warnings)
{
    for (const auto& [source, executable] : std::vector<std::pair<NomadString, bool>>{
        {"class Point\nvalue = 1\nend\nfun write point:Point\npoint.value = 2\nend", false},
        {"open class Point\nfun read\nreturn this.extra\nend\nend\n"
            "fun write point:Point\npoint.extra = 1\nend", false},
        {"open class Point\nend\nfun read point:Point\nreturn $\"value {point.extra}\"\nend\n"
            "fun write point:Point\npoint.extra = 1\nend", true}
    }) {
        LanguageTestFixture fixture;
        fixture.addFunction("main", source);
        CompilerContext context(&fixture.getCompiler());
        const auto compiled = fixture.getCompiler().compileScripts(&context);
        BOOST_TEST(compiled == executable);
        BOOST_TEST(context.getWarningCount() == 0u);
        for (const auto& diagnostic : context.getDiagnostics()) {
            BOOST_TEST(diagnostic.message.find("Unknown class field") == NomadString::npos);
        }
    }
}

BOOST_AUTO_TEST_CASE(external_fields_work_on_native_open_types_and_preserve_native_properties)
{
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    const auto id = runtime.createObjectType("Host", ObjectTypeMode::Opened);
    BOOST_REQUIRE(id.isValid());
    BOOST_REQUIRE(runtime.addProperty(id, {
        .valueTypeId = runtime.getIntegerType()->getId(),
        .name = "nativeValue",
        .getter = [](VirtualMachine*, RuntimeValue& value) { value = RuntimeValue{NomadInteger{1}}; },
        .setter = {},
        .documentation = ""
    }).isValid());
    fixture.addFunction("main",
        "fun use host:Host\nhost.extra = 1\nreturn host.extra + host.nativeValue\nend"
    );
    CompilerContext context(&fixture.getCompiler());
    BOOST_TEST(fixture.getCompiler().compileScripts(&context));
    BOOST_TEST(context.getWarningCount() == 0u);
    BOOST_TEST(context.getErrorCount() == 0u);
    const auto* type = runtime.getTypeByName("Host")->asObjectType();
    BOOST_REQUIRE(type->getProperty("extra") != nullptr);
    BOOST_TEST(type->getFieldIndex(type->getProperty("extra")->getId()) == 0u);
    BOOST_TEST(type->getFieldIndex(type->getProperty("nativeValue")->getId()) == NOMAD_INVALID_INDEX);
}

BOOST_AUTO_TEST_CASE(external_fields_resolve_nested_receivers_and_captured_aliases)
{
    LanguageTestFixture fixture;
    registerCallbackTestFunctions(fixture.getRuntime());
    fixture.addFunction("main",
        "open class Inner\nend\nclass Outer\nend\n"
        "fun callback point:Inner\n"
        "result = factory fun item\npoint.extra = item\nreturn point.extra\nend\nreturn result\nend\n"
        "fun nested outer:Outer inner:Inner\nreturn outer.inner.extra\nend"
    );
    fixture.getCompiler().registerPhaseHook([&](CompilerContext*, const CompilationPhase phase) {
        if (phase != CompilationPhase::Parse) return;
        auto& runtime = fixture.getRuntime();
        BOOST_REQUIRE(runtime.registerProperty(
            runtime.getTypeByName("Outer")->asObjectType(), "inner", runtime.getTypeByName("Inner")
        ).isValid());
    });
    CompilerContext context(&fixture.getCompiler());
    BOOST_TEST(fixture.getCompiler().compileScripts(&context));
    BOOST_TEST(context.getWarningCount() == 0u);
    const auto* type = fixture.getRuntime().getTypeByName("Inner")->asObjectType();
    BOOST_REQUIRE(type->getProperty("extra") != nullptr);
    BOOST_TEST(context.getErrorCount() == 0u);
}

BOOST_AUTO_TEST_CASE(open_hubs_still_reject_external_member_introduction)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "open hub test\nv1 = 1\nend\ntest.v2 = 2");
    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.hasError("Hub members must be declared inside a hub block"));
}

BOOST_AUTO_TEST_CASE(stored_fields_are_not_parsed_as_other_types_native_methods)
{
    for (const auto sourceParameter : {false, true}) {
        LanguageTestFixture fixture;
        auto& runtime = fixture.getRuntime();
        const auto id = runtime.createObjectType("Other", ObjectTypeMode::Closed);
        BOOST_REQUIRE(id.isValid());
        BOOST_REQUIRE(runtime.addMethod(id, {
            .name = "extra",
            .parameters = {{
                sourceParameter ? runtime.getLineNumberType()->getId() : runtime.getIntegerType()->getId(), "value", ""
            }},
            .returnTypeId = runtime.getIntegerType()->getId(),
            .function = [](VirtualMachine* machine) { machine->setIntegerResult(1); },
            .documentation = ""
        }).isValid());
        fixture.addFunction("main",
            "open class Point\nend\n"
            "fun read point:Point\nalias = point\nalias.extra = 42\nreturn alias.extra\nend"
        );
        CompilerContext context(&fixture.getCompiler());
        BOOST_TEST(fixture.getCompiler().compileScripts(&context));
        BOOST_TEST(context.getWarningCount() == 0u);
        BOOST_TEST(context.getErrorCount() == 0u);
        const auto* read = runtime.getScriptFunction(runtime.getFunctionId("read"));
        BOOST_REQUIRE(read != nullptr);
        BOOST_TEST(read->getReturnType() == runtime.getIntegerType());
    }
}

BOOST_AUTO_TEST_CASE(missing_native_method_arguments_still_report_source_errors)
{
    LanguageTestFixture fixture;
    fixture.addFunction("main", "value = \"text\"\nreturn value.contains");
    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.hasError("Incorrect argument count"));
    BOOST_TEST(!fixture.hasError("Internal error"));
}

BOOST_AUTO_TEST_SUITE_END()
