// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>

#include <nomad/compiler/ClassDeclaration.hpp>
#include <nomad/compiler/Parser.hpp>
#include <nomad/compiler/Tokenizer.hpp>

#include <nomad/script/Hub.hpp>
#include <nomad/script/Interpreter.hpp>
#include <nomad/script/Type.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(language_classes)

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

    auto compiler = runtime.createCompiler();
    CompilerContext context(compiler.get());
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
    const auto* diagnostic = fixture.findError("Class members are not supported yet");
    BOOST_REQUIRE(diagnostic != nullptr);
    BOOST_TEST(diagnostic->sourceName == "main.nomad");
    BOOST_TEST(diagnostic->line == 2u);
    BOOST_TEST(fixture.getRuntime().getTypeByName("Point") == nullptr);
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

BOOST_AUTO_TEST_SUITE_END()
