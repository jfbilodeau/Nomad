// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <LanguageTestFixture.hpp>
#include <TestDirectory.hpp>

#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Closure.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <boost/test/unit_test.hpp>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace nomad;
using namespace nomad::test;

BOOST_AUTO_TEST_SUITE(language_script_discovery)

BOOST_AUTO_TEST_CASE(script_names_are_derived_from_relative_paths)
{
    TestDirectory directory("nomad_language_test_names");
    directory.write("init.nomad", "return 1");
    directory.write("entities/player.nomad", "return 2");
    directory.write("scene/play/intro.nomad", "return 3");
    directory.write("notes.txt", "this is not a function");
    directory.write("entities/readme.md", "neither is this");

    LanguageTestFixture fixture;
    fixture.addSources(loadSourcePaths(std::vector{directory.getPath()}));

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());

    auto& runtime = fixture.getRuntime();
    BOOST_TEST(runtime.getScriptFunctionId("init").isValid());
    BOOST_TEST(runtime.getScriptFunctionId("entities.player").isValid());
    BOOST_TEST(runtime.getScriptFunctionId("scene.play.intro").isValid());
    BOOST_TEST(runtime.getScriptFunctionId("notes").isInvalid());
    BOOST_TEST(runtime.getScriptFunctionId("entities.readme").isInvalid());

    BOOST_TEST(fixture.execute("init").integerValue == 1);
    BOOST_TEST(fixture.execute("entities.player").integerValue == 2);
    BOOST_TEST(fixture.execute("scene.play.intro").integerValue == 3);
}

BOOST_AUTO_TEST_CASE(discovered_functions_call_each_other)
{
    TestDirectory directory("nomad_language_test_calls");
    directory.write("init.nomad", "return entities.player.speed 3");
    directory.write("entities/player/speed.nomad", "params factor:int\nreturn factor * 5");

    LanguageTestFixture fixture;
    fixture.addSources(loadSourcePaths(std::vector{directory.getPath()}));

    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.execute("init").integerValue == 15);
}

BOOST_AUTO_TEST_CASE(object_script_roots_compile_receiver_bound_initializers_and_shared_methods) {
    TestDirectory directory("nomad_object_script_methods");
    directory.write("types.nomad", "class Entity\nend");
    directory.write("entities/player.nomad",
        "params start:int\nthis.x = start\n(this).y = 0\n"
        "fun moveHome\nthis.x = 0\nthis.y = 0\nreturn this.x + this.y\nend\n"
        "fun move amount:int\nthis.x = this.x + amount\nreturn this.x\nend\n"
        "fun move amount:float\nreturn amount + 0.5\nend\n"
        "this.move 2\nreturn $\"entity:{this.x}\"\n");
    directory.write("entities/other.nomad",
        "this.extra = 7\nfun moveOther amount:int\nthis.x = this.x + amount\nreturn this.x\nend");
    directory.write("main.nomad", "entity = new Entity\nreturn entity.move 3");

    LanguageTestFixture fixture;
    const std::vector<SourceRoot> sources{{directory.getPath()}};
    const std::vector<ObjectScriptRoot> roots{{directory.getPath() / "entities", "Entity"}};
    fixture.getCompiler().registerSourceRoots(sources);
    fixture.getCompiler().registerObjectScriptRoots(roots);
    fixture.addSources({});
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    auto& runtime = fixture.getRuntime();
    const auto* type = runtime.getTypeByName("Entity")->asClassType();
    const auto initializer = runtime.resolveMethod(type, "entities.player", {runtime.getIntegerType()});
    const auto moveHome = runtime.resolveMethod(type, "moveHome", {});
    const auto moveOther = runtime.resolveMethod(type, "moveOther", {runtime.getIntegerType()});
    BOOST_REQUIRE(initializer.isResolved());
    BOOST_REQUIRE(moveHome.isResolved());
    BOOST_REQUIRE(moveOther.isResolved());
    BOOST_TEST(runtime.getScriptFunctionId("entities.player").isInvalid());
    BOOST_TEST(runtime.getScriptFunctionId("moveHome").isInvalid());
    BOOST_TEST(fixture.execute("main").integerValue == 3);

    const Interpreter interpreter(fixture.getVirtualMachine());
    BOOST_CHECK_THROW((void)interpreter.runFunction(initializer.id, {}), std::invalid_argument);
    const auto first = interpreter.construct(type->getConstructors().front()->getId(), {});
    const auto second = interpreter.construct(type->getConstructors().front()->getId(), {});
    const InterpreterValue start(runtime.getIntegerType(), RuntimeValue{NomadInteger{11}});
    const auto result = interpreter.runObjectMethod(first, initializer.id, {&start, 1});
    BOOST_TEST(result.getValue().getStringValue() == "entity:13");
    auto firstReceiver = interpreter.createObjectReceiver(*type, first.getValue().getObjectValue());
    auto secondReceiver = interpreter.createObjectReceiver(*type, second.getValue().getObjectValue());
    BOOST_TEST(firstReceiver->getProperty("x").getValue().getIntegerValue() == 13);
    BOOST_TEST(secondReceiver->getProperty("x").getValue().getIntegerValue() == 0);
    BOOST_TEST(interpreter.runObjectMethod(first, moveOther.id, {&start, 1})
        .getValue().getIntegerValue() == 24);
    BOOST_TEST(interpreter.runObjectMethod(first, moveHome.id, {}).getValue().getIntegerValue() == 0);
    BOOST_TEST(secondReceiver->getProperty("x").getValue().getIntegerValue() == 0);
}

BOOST_AUTO_TEST_CASE(object_script_inline_callbacks_capture_the_live_receiver) {
    LanguageTestFixture fixture;
    auto& runtime = fixture.getRuntime();
    std::unique_ptr<Closure> callback;
    const auto* callbackType = runtime.getTypeManager().getCallbackType({}, runtime.getIntegerType());
    BOOST_REQUIRE(runtime.registerNativeFunction(
        "keepCallback",
        [&](VirtualMachine* machine) {
            callback = machine->createClosure(&runtime, machine->getCallableIdParameter(0));
        },
        {defParameter("callback", callbackType, "")},
        runtime.getVoidType(),
        ""
    ).isValid());
    fixture.addSources({{
        {"types", {"types.nomad", "class Entity\nend"}},
        {"entities.player", {"editor:player",
            "this.value = 42\nkeepCallback fun\nreturn this.value\nend"}, {}, {}, "Entity"}
    }, {}});
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    const auto* type = runtime.getTypeByName("Entity")->asClassType();
    const auto initializer = runtime.resolveMethod(type, "entities.player", {});
    BOOST_REQUIRE(initializer.isResolved());
    const Interpreter interpreter(fixture.getVirtualMachine());
    const auto entity = interpreter.construct(type->getConstructors().front()->getId(), {});
    (void)interpreter.runObjectMethod(entity, initializer.id, {});
    BOOST_REQUIRE(callback != nullptr);
    auto receiver = interpreter.createObjectReceiver(*type, entity.getValue().getObjectValue());
    const InterpreterValue value(runtime.getIntegerType(), RuntimeValue{NomadInteger{49}});
    receiver->setProperty("value", value);
    BOOST_TEST(interpreter.runFunction(*callback, {}).getValue().getIntegerValue() == 49);
}

BOOST_AUTO_TEST_CASE(object_script_receiver_and_method_errors_preserve_class_rules) {
    struct InvalidObjectScript {
        NomadString receiver;
        NomadString body;
        NomadString otherBody;
        NomadString diagnostic;
    };
    for (const auto& scenario : std::vector<InvalidObjectScript>{
        {"Missing", "return 1", "", "public class receiver"},
        {"int", "return 1", "", "public class receiver"},
        {"Entity", "params this:Entity\nreturn 1", "", "cannot be a parameter"},
        {"Entity", "this.value = 1", "this.value = \"wrong\"", "Cannot assign type"},
        {"Entity", "this.value = 1\nhub shared\nvalue = this.value\nend", "", "cannot read the object receiver"},
        {"Entity", "this.value = 1\nfun reset\nthis.extra = 2\nend", "", "Unknown class field 'extra'"},
        {"Entity", "this.value = 1\nfun reset\nthis.value = 0\nend\nreset", "", "Unknown statement"},
        {"Entity", "fun reset\nend", "fun reset\nend", "Cannot register object script method 'reset'"}
    }) {
        BOOST_TEST_CONTEXT(scenario.body) {
            LanguageTestFixture fixture;
            fixture.addSources({{
                {"types", {"types.nomad", "class Entity\nend"}},
                {"entities.player", {"entities/player.nomad", scenario.body}, {}, {}, scenario.receiver},
                {"entities.other", {"entities/other.nomad", scenario.otherBody}, {}, {}, scenario.receiver}
            }, {}});
            BOOST_TEST(!fixture.compile());
            BOOST_TEST(fixture.hasError(scenario.diagnostic), fixture.getDiagnostics());
        }
    }
}

BOOST_AUTO_TEST_CASE(object_script_mappings_preserve_editor_snapshots_and_reject_conflicting_receivers) {
    TestDirectory directory("nomad_object_script_snapshots");
    directory.write("types.nomad", "class Entity\nend\nclass Other\nend");
    const auto player = directory.write("entities/player.nomad", "return missing");
    const std::vector<ScriptSource> snapshots{
        {"ignored", {"editor:player", "this.value = 7\nreturn this.value", player}}
    };
    {
        LanguageTestFixture fixture;
        const std::vector<ObjectScriptRoot> roots{{directory.getPath() / "entities", "Entity"}};
        fixture.getCompiler().registerObjectScriptRoots(roots);
        fixture.addSources(loadSourcePaths(std::vector{directory.getPath()}, snapshots));
        BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
        auto& runtime = fixture.getRuntime();
        const auto* type = runtime.getTypeByName("Entity")->asClassType();
        const auto initializer = runtime.resolveMethod(type, "entities.player", {});
        BOOST_REQUIRE(initializer.isResolved());
        const Interpreter interpreter(fixture.getVirtualMachine());
        const auto entity = interpreter.construct(type->getConstructors().front()->getId(), {});
        BOOST_TEST(interpreter.runObjectMethod(entity, initializer.id, {}).getValue().getIntegerValue() == 7);
    }

    LanguageTestFixture fixture;
    const std::vector<ObjectScriptRoot> roots{
        {directory.getPath(), "Other"}, {directory.getPath() / "entities", "Entity"}
    };
    fixture.getCompiler().registerObjectScriptRoots(roots);
    fixture.addSources(loadSourcePaths(std::vector{directory.getPath()}, snapshots));
    BOOST_TEST(!fixture.compile());
    BOOST_TEST(fixture.hasError("conflicting receiver types"));
}

BOOST_AUTO_TEST_CASE(object_script_assignments_preserve_native_property_permissions) {
    for (const auto* body : {"return this.readOnly", "this.readOnly = 1"}) {
        LanguageTestFixture fixture;
        auto& runtime = fixture.getRuntime();
        const auto typeId = runtime.registerClass("NativeReceiver", ClassMode::Closed);
        BOOST_REQUIRE(runtime.registerNativeClassProperty(typeId, {
            .valueType = runtime.getIntegerType(),
            .name = "readOnly",
            .getter = [](VirtualMachine*, RuntimeValue& result) {
                result.setIntegerValue(42);
            },
            .setter = {}
        }).isValid());
        fixture.addSources({{
            {"objects.read", {"objects/read.nomad", body}, {}, {}, "NativeReceiver"}
        }, {}});
        if (NomadStringView(body).starts_with("this.readOnly =")) {
            BOOST_TEST(!fixture.compile());
            BOOST_TEST(fixture.hasError("read-only"), fixture.getDiagnostics());
        } else {
            BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
            const auto resolved = runtime.resolveMethod(runtime.getTypeByName("NativeReceiver"), "objects.read", {});
            BOOST_REQUIRE(resolved.isResolved());
        }
    }
}

BOOST_AUTO_TEST_CASE(compile_errors_report_source_file)
{
    TestDirectory directory("nomad_language_test_errors");
    directory.write("good.nomad", "return 1");
    directory.write("sub/bad.nomad", "a = 1\nreturn missing");

    LanguageTestFixture fixture;
    fixture.addSources(loadSourcePaths(std::vector{directory.getPath()}));

    BOOST_REQUIRE(!fixture.compile());

    const auto* error = fixture.findError("Unknown identifier");
    BOOST_REQUIRE_MESSAGE(error != nullptr, fixture.getDiagnostics());
    BOOST_TEST(error->sourceName.find("bad.nomad") != std::string::npos, error->sourceName);
    BOOST_TEST(error->line == 2u);
}

BOOST_AUTO_TEST_CASE(file_and_directory_name_collision_is_rejected)
{
    TestDirectory directory("nomad_language_test_collision");
    directory.write("scene/play/intro.nomad", "return 1");
    directory.write("scene.play.intro.nomad", "return 2");

    LanguageTestFixture fixture;
    BOOST_CHECK_NO_THROW(fixture.addSources(loadSourcePaths(std::vector{directory.getPath()})));

    // Both files map to the function name `scene.play.intro`.
    BOOST_REQUIRE(!fixture.compile());

    const auto* error = fixture.findError("scene.play.intro");
    BOOST_REQUIRE_MESSAGE(error != nullptr, fixture.getDiagnostics());
    BOOST_TEST(error->message.find("already used") != std::string::npos, error->message);
}

BOOST_AUTO_TEST_CASE(function_colliding_with_function_file_is_rejected)
{
    TestDirectory directory("nomad_language_test_function_collision");
    directory.write("scene/play/intro.nomad", "return 1");
    directory.write("init.nomad", "fun scene.play.intro\nend");

    LanguageTestFixture fixture;
    fixture.addSources(loadSourcePaths(std::vector{directory.getPath()}));

    BOOST_TEST(!fixture.compile());
}

BOOST_AUTO_TEST_CASE(symbolic_linked_directories_are_not_scanned)
{
    TestDirectory directory("nomad_language_test_symlink");
    directory.write("init.nomad", "return 1");

    std::error_code error;
    std::filesystem::create_directory_symlink(
        directory.getPath() / "missing-target",
        directory.getPath() / "linked",
        error
    );

    if (error) {
        BOOST_TEST_MESSAGE("Directory symbolic links are unavailable: " << error.message());
        return;
    }

    LanguageTestFixture fixture;
    BOOST_CHECK_NO_THROW(fixture.addSources(loadSourcePaths(std::vector{directory.getPath()})));
    BOOST_REQUIRE_MESSAGE(fixture.compile(), fixture.getDiagnostics());
    BOOST_TEST(fixture.getRuntime().getScriptFunctionId("init").isValid());
}

BOOST_AUTO_TEST_CASE(directory_depth_limit_is_preserved)
{
    TestDirectory directory("nomad_language_test_depth");
    auto relative = std::filesystem::path{};
    for (auto index = 0; index < 9; ++index) {
        relative /= "nested";
    }
    directory.write(relative / "included.nomad", "return 1");
    directory.write(relative / "nested" / "excluded.nomad", "return missing");

    const auto batch = loadSourcePaths(std::vector{directory.getPath()});
    BOOST_REQUIRE(batch.diagnostics.empty());
    BOOST_REQUIRE(batch.sources.size() == 1U);
    BOOST_TEST(batch.sources.front().scriptName.ends_with(".included"));
}

BOOST_AUTO_TEST_SUITE_END()
