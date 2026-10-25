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
        "class Point\nfun move\nthis.move\nend\nend"
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
        BOOST_TEST(fixture.hasError("Script class field"));
        const auto localId = runtime.getVariableContext(contextId)->getFirstVariableId();
        BOOST_TEST(!runtime.getVariableContext(contextId)->isRead(localId));
        BOOST_TEST(!runtime.getVariableContext(contextId)->isWritten(localId));
    }
}

BOOST_AUTO_TEST_SUITE_END()
