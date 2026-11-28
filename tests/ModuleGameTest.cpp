// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Game.hpp>
#include <nomad/game/Scene.hpp>
#include <nomad/game/SceneObject.hpp>

#include <nomad/game/resource/SpritePacker.hpp>

#include <nomad/interpreter/Interpreter.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Hub.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/RuntimeValueGuard.hpp>
#include <nomad/script/VirtualMachine.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <SDL3/SDL.h>

#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

struct ModuleGameFixture {
    TestDirectory directory{"nomad_module_game"};
    bool entryExecuted = false;
    Runtime runtime;
    std::unique_ptr<VirtualMachine> virtualMachine = runtime.createVirtualMachine();
    GameOptions options;

    ModuleGameFixture() {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        directory.write("text/text.csv", "key,en\ntitle,Module test\n");
        directory.write("scripts/init.nomad", "this is not valid Nomad code\n");
        options.resourcePath = directory.getPath().string();
        options.audio.directories = std::vector<NomadPath>{};
        options.sprites.directories = std::vector<NomadPath>{};
        runtime.registerNativeFunction(
            "markEntryExecuted",
            [this](VirtualMachine*) {
                entryExecuted = true;
            },
            {},
            runtime.getVoidType(),
            NomadDoc("")
        );
        compile("init", "markEntryExecuted\n");
    }

    CallableId compile(const NomadString& name, const NomadString& source) {
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{{name, {name + ".nomad", source}}}, {}});
        BOOST_REQUIRE(compiler->compileFunctions(&context));
        return runtime.getScriptFunctionId(name);
    }

    CallableId compileSceneObjectScript(const NomadString& name, const NomadString& source) {
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{
            {name, {name + ".nomad", source}, {}, {}, "game.Scene"}
        }, {}});
        const auto compiled = compiler->compileFunctions(&context);
        const auto error = context.getFirstError();
        const auto diagnostic = error.has_value() ? error->message : "Scene object script did not compile";
        BOOST_REQUIRE_MESSAGE(compiled, diagnostic);
        const auto method = runtime.resolveMethod(runtime.getTypeByName("game.Scene"), name, {});
        BOOST_REQUIRE(method.isResolved());

        return method.id;
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(module_game, ModuleGameFixture)

BOOST_AUTO_TEST_CASE(game_native_api_is_registered_under_hubs)
{
    auto game = Game::createForRuntime(&runtime, options, true);

    for (const auto* hubName : {
        "game",
        "game.audio",
        "game.music",
        "game.debug",
        "game.debug.console",
        "game.debug.draw",
    }) {
        BOOST_TEST(runtime.getHubId(hubName).isValid());
    }

    BOOST_TEST(runtime.getHubId("game.inventory").isInvalid());
    BOOST_TEST(runtime.getHubId("game.settings").isInvalid());
    for (const auto* name : {
        "game.inventory.save", "game.inventory.load", "game.inventory.saveExists",
        "game.settings.save", "game.settings.load"
    }) {
        BOOST_TEST_CONTEXT(name) {
            BOOST_TEST(runtime.getFunctionId(name).isInvalid());
        }
    }

    const auto* gameHub = runtime.getHub(runtime.getHubId("game"));
    BOOST_REQUIRE(gameHub != nullptr);
    const auto* volumeMember = gameHub->getMember("volume");
    BOOST_REQUIRE(volumeMember != nullptr);
    BOOST_TEST(std::holds_alternative<DynamicVariableId>(*volumeMember));
    BOOST_TEST(
        std::get<DynamicVariableId>(*volumeMember) == runtime.getDynamicVariableId("game.volume")
    );
    const auto* createSceneMember = gameHub->getMember("createScene");
    BOOST_REQUIRE(createSceneMember != nullptr);
    BOOST_TEST(std::holds_alternative<HubFunctions>(*createSceneMember));

    const auto* audioHub = runtime.getHub(runtime.getHubId("game.audio"));
    BOOST_REQUIRE(audioHub != nullptr);
    const auto* loadAudioMember = audioHub->getMember("load");
    BOOST_REQUIRE(loadAudioMember != nullptr);
    BOOST_TEST(std::holds_alternative<HubFunctions>(*loadAudioMember));

    BOOST_TEST(runtime.getContextId("inventory").isInvalid());
    BOOST_TEST(runtime.getContextId("settings").isInvalid());
    BOOST_TEST(runtime.getVariableContextIdByPrefix("game.inventory.gold").isInvalid());

    const auto declarationFunctionId = compile(
        "declareGameDataHubs",
        "hub game.inventory\n"
        "fun save\n"
        "return 42\n"
        "end\n"
        "end\n"
        "hub game.settings\n"
        "fun save\n"
        "return 7\n"
        "end\n"
        "end"
    );
    BOOST_TEST(declarationFunctionId.isValid());

    const auto* inventoryHub = runtime.getHub(runtime.getHubId("game.inventory"));
    BOOST_REQUIRE(inventoryHub != nullptr);
    const auto* inventorySaveMember = inventoryHub->getMember("save");
    BOOST_REQUIRE(inventorySaveMember != nullptr);
    BOOST_TEST(std::holds_alternative<HubFunctions>(*inventorySaveMember));
    RuntimeValue result;
    virtualMachine->executeFunction(runtime.getScriptFunctionId("game.inventory.save"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 42);

    const auto* settingsHub = runtime.getHub(runtime.getHubId("game.settings"));
    BOOST_REQUIRE(settingsHub != nullptr);
    const auto* settingsSaveMember = settingsHub->getMember("save");
    BOOST_REQUIRE(settingsSaveMember != nullptr);
    BOOST_TEST(std::holds_alternative<HubFunctions>(*settingsSaveMember));
    virtualMachine->executeFunction(runtime.getScriptFunctionId("game.settings.save"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 7);
}

BOOST_AUTO_TEST_CASE(borrowed_runtime_remains_usable_after_game_destruction)
{
    auto game = Game::createForRuntime(&runtime, options, true);
    BOOST_TEST(game->getRuntime() == &runtime);
    game.reset();

    const auto functionId = compile("afterGame", "return 42\n");
    RuntimeValue result;
    virtualMachine->executeFunction(functionId, {}, result);
    BOOST_TEST(result.getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(current_scene_returns_stable_references_and_methods_use_their_receiver) {
    auto game = Game::createForRuntime(&runtime, options, true);
    const Interpreter interpreter(*virtualMachine);
    const auto accessorId = runtime.getFunctionId("game.currentScene");
    const auto reportsNoScene = [](const NomadException& error) {

        return NomadString(error.what()).find("There is no current scene") != NomadString::npos;
    };
    BOOST_CHECK_EXCEPTION(
        (void)interpreter.runFunction(accessorId, {}), NomadException, reportsNoScene
    );

    Scene first(game.get(), 1);
    Scene second(game.get(), 2);
    first.addEntity(std::make_unique<Entity>(&first, game->getEntityVariables(), 1, 0.0f, 0.0f, 0));
    second.addEntity(std::make_unique<Entity>(&second, game->getEntityVariables(), 1, 0.0f, 0.0f, 0));
    game->getCurrentContext()->reset(&first, nullptr);
    const auto firstReference = interpreter.runFunction(accessorId, {});
    const auto alias = interpreter.runFunction(accessorId, {});
    BOOST_TEST(firstReference.getValue().getObjectValue() == alias.getValue().getObjectValue());
    BOOST_TEST(firstReference.getValue().getObjectValue() == first.getSceneObject());

    game->getCurrentContext()->reset(&second, nullptr);
    const auto secondReference = interpreter.runFunction(accessorId, {});
    BOOST_TEST(firstReference.getValue().getObjectValue() != secondReference.getValue().getObjectValue());
    const auto pauseId = compile("pauseSceneReference", "params target:game.Scene\ntarget.pauseAll");
    runtime.initializeScripts();
    virtualMachine->executeFunction(pauseId, {firstReference.getValue()});
    BOOST_TEST(first.getEntityById(1)->isPaused());
    BOOST_TEST(!second.getEntityById(1)->isPaused());
    BOOST_TEST(game->getCurrentContext()->getScene() == &second);

    const auto unpause = runtime.resolveMethod(firstReference.getType(), "unpauseAll", {});
    BOOST_REQUIRE(unpause.isResolved());
    (void)interpreter.runObjectMethod(firstReference, unpause.id, {});
    BOOST_TEST(!first.getEntityById(1)->isPaused());
    BOOST_TEST(runtime.getNativeFunctionId("scene.pauseAll").isInvalid());
    BOOST_TEST(runtime.getNativeFunctionId("scene.unpauseAll").isInvalid());
    BOOST_TEST(runtime.getNativeFunctionId("scene.onUpdate").isValid());
    BOOST_TEST(runtime.getDynamicVariableId("scene.camera.x").isValid());
}

BOOST_AUTO_TEST_CASE(scene_references_expire_after_queued_removal_and_game_destruction) {
    auto game = Game::createForRuntime(&runtime, options, true);
    const auto firstInitializer = compile("initializeFirstScene", "global.firstScene = game.currentScene");
    const auto secondInitializer = compile("initializeSecondScene", "global.secondScene = game.currentScene");
    runtime.initializeScripts();
    const auto firstId = game->createScene("first", firstInitializer);
    const auto secondId = game->createScene("second", secondInitializer);
    auto polls = 0;
    game->setExitHandler({}, [&] {

        return ++polls > 16 ||
            (game->getSceneById(firstId) != nullptr && game->getSceneById(secondId) != nullptr);
    });
    game->run();
    BOOST_REQUIRE(game->getSceneById(firstId) != nullptr);
    BOOST_REQUIRE(game->getSceneById(secondId) != nullptr);

    RuntimeValue firstValue;
    RuntimeValue secondValue;
    runtime.getContextVariableValue(runtime.getContextVariableId("global.firstScene"), firstValue);
    runtime.getContextVariableValue(runtime.getContextVariableId("global.secondScene"), secondValue);
    const auto* type = runtime.getTypeByName("game.Scene");
    const InterpreterValue firstReference(type, firstValue);
    const InterpreterValue secondReference(type, secondValue);
    const Interpreter interpreter(*virtualMachine);
    const auto remove = runtime.resolveMethod(type, "removeSelf", {});
    const auto pause = runtime.resolveMethod(type, "pauseAll", {});
    BOOST_REQUIRE(remove.isResolved());
    BOOST_REQUIRE(pause.isResolved());
    (void)interpreter.runObjectMethod(firstReference, remove.id, {});
    (void)interpreter.runObjectMethod(firstReference, pause.id, {});
    BOOST_TEST(game->getSceneById(firstId) != nullptr);

    polls = 0;
    game->setExitHandler({}, [&] {

        return ++polls > 16 || game->getSceneById(firstId) == nullptr;
    });
    game->run();
    BOOST_TEST(game->getSceneById(firstId) == nullptr);
    BOOST_TEST(game->getSceneById(secondId) != nullptr);
    const auto reportsRemovedScene = [](const NomadException& error) {

        return NomadString(error.what()).find("Scene is no longer available") != NomadString::npos;
    };
    BOOST_CHECK_EXCEPTION(
        (void)interpreter.runObjectMethod(firstReference, pause.id, {}),
        NomadException,
        reportsRemovedScene
    );
    (void)interpreter.runObjectMethod(secondReference, pause.id, {});
    game.reset();
    BOOST_CHECK_EXCEPTION(
        (void)interpreter.runObjectMethod(secondReference, pause.id, {}),
        NomadException,
        reportsRemovedScene
    );
}

BOOST_AUTO_TEST_CASE(native_scene_object_fields_share_storage_and_keep_instances_independent) {
    auto game = Game::createForRuntime(&runtime, options, true);
    const auto initializer = compileSceneObjectScript(
        "scenes.storedFields",
        "struct SceneState\nnumber = 0\ntext = \"\"\n"
        "fun add amount:int\nthis.number = this.number + amount\nreturn this.number\nend\nend\n"
        "this.counter = 10\nthis.label = \"initialized\"\nthis.state = new SceneState\n"
        "this.state.number = 3\nthis.state.text = \"initial state\"\n"
        "fun advance\nthis.counter = this.counter + 1\nthis.state.add 2\n"
        "this.state.text = \"compiled state\"\nreturn this.counter\nend\n"
        "return this.counter"
    );
    runtime.initializeScripts();
    Scene first(game.get(), 1);
    Scene second(game.get(), 2);
    const auto* type = runtime.getTypeByName("game.Scene")->asClassType();
    RuntimeValue firstValue;
    firstValue.setObjectValue(first.getSceneObject());
    RuntimeValue secondValue;
    secondValue.setObjectValue(second.getSceneObject());
    const InterpreterValue firstReference(type, firstValue);
    const InterpreterValue alias(type, firstValue);
    const InterpreterValue secondReference(type, secondValue);
    const Interpreter interpreter(*virtualMachine);
    auto firstReceiver = interpreter.createObjectReceiver(*type, first.getSceneObject());
    auto aliasReceiver = interpreter.createObjectReceiver(*type, alias.getValue().getObjectValue());
    auto secondReceiver = interpreter.createObjectReceiver(*type, second.getSceneObject());
    BOOST_TEST(firstReceiver->getProperty("counter").getValue().getIntegerValue() == 0);
    BOOST_TEST(firstReceiver->getProperty("label").getValue().getStringValue() == "");
    BOOST_TEST(firstReceiver->getProperty("state.number").getValue().getIntegerValue() == 0);
    BOOST_TEST(interpreter.runObjectMethod(firstReference, initializer, {}).getValue().getIntegerValue() == 10);
    const auto advance = runtime.resolveMethod(type, "advance", {});
    BOOST_REQUIRE(advance.isResolved());
    BOOST_TEST(interpreter.runObjectMethod(alias, advance.id, {}).getValue().getIntegerValue() == 11);
    BOOST_TEST(firstReceiver->getProperty("counter").getValue().getIntegerValue() == 11);
    BOOST_TEST(aliasReceiver->getProperty("state.number").getValue().getIntegerValue() == 5);
    BOOST_TEST(aliasReceiver->getProperty("state.text").getValue().getStringValue() == "compiled state");
    BOOST_TEST(secondReceiver->getProperty("counter").getValue().getIntegerValue() == 0);
    BOOST_TEST(secondReceiver->getProperty("label").getValue().getStringValue() == "");

    const auto state = firstReceiver->getProperty("state");
    secondReceiver->setProperty("state", state);
    const InterpreterValue number(runtime.getIntegerType(), RuntimeValue{NomadInteger{99}});
    secondReceiver->setProperty("state.number", number);
    BOOST_TEST(firstReceiver->getProperty("state.number").getValue().getIntegerValue() == 5);
    BOOST_TEST(secondReceiver->getProperty("state.number").getValue().getIntegerValue() == 99);
    InterpreterContext context(interpreter);
    context.setVariable("current", type, firstValue);
    BOOST_REQUIRE(interpreter.execute("current.counter = 25", context));
    BOOST_REQUIRE(interpreter.execute("current.state.add 4", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 9);
    BOOST_TEST(aliasReceiver->getProperty("counter").getValue().getIntegerValue() == 25);
    BOOST_TEST(aliasReceiver->getProperty("state.number").getValue().getIntegerValue() == 9);
    BOOST_TEST(secondReceiver->getProperty("state.number").getValue().getIntegerValue() == 99);
}

BOOST_AUTO_TEST_CASE(native_scene_removal_releases_owned_fields_even_with_retained_self_references) {
    auto game = Game::createForRuntime(&runtime, options, true);
    const auto initializer = compileSceneObjectScript(
        "scenes.ownedFields",
        "class ScenePayload\nend\n"
        "this.payload = new ScenePayload\nthis.self = this\n"
        "this.counter = 42\nthis.label = $\"scene-{this.counter}\""
    );
    runtime.initializeScripts();
    const auto objectsBefore = NomadObject::getLiveObjectCount();
    const auto stringsBefore = RuntimeValue::getLiveStringCount();
    auto scene = std::make_unique<Scene>(game.get(), 1);
    const auto* type = runtime.getTypeByName("game.Scene")->asClassType();
    RuntimeValue value;
    value.setObjectValue(scene->getSceneObject());
    const InterpreterValue reference(type, value);
    const Interpreter interpreter(*virtualMachine);
    auto receiver = interpreter.createObjectReceiver(*type, reference.getValue().getObjectValue());
    (void)interpreter.runObjectMethod(reference, initializer, {});
    BOOST_TEST(receiver->getProperty("label").getValue().getStringValue() == "scene-42");
    scene.reset();
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore + 1);
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
    const auto reportsMissingStorage = [](const NomadException& error) {

        return NomadString(error.what()).find("Stored property receiver has no matching field storage") !=
            NomadString::npos;
    };
    BOOST_CHECK_EXCEPTION(
        (void)receiver->getProperty("counter"), NomadException, reportsMissingStorage
    );
    const InterpreterValue replacement(runtime.getIntegerType(), RuntimeValue{NomadInteger{1}});
    BOOST_CHECK_EXCEPTION(
        receiver->setProperty("counter", replacement), NomadException, reportsMissingStorage
    );
    BOOST_TEST(reference.getValue().getObjectValue()->getReferenceCount() == 2U);
    BOOST_TEST(NomadObject::getLiveObjectCount() == objectsBefore + 1);
    BOOST_TEST(RuntimeValue::getLiveStringCount() == stringsBefore);
}

BOOST_AUTO_TEST_CASE(native_scene_field_layouts_do_not_expand_after_creation) {
    auto game = Game::createForRuntime(&runtime, options, true);
    Scene existing(game.get(), 1);
    const auto initializer = compileSceneObjectScript(
        "scenes.newFields", "this.counter = 42\nreturn this.counter"
    );
    runtime.initializeScripts();
    Scene fresh(game.get(), 2);
    const auto* type = runtime.getTypeByName("game.Scene");
    RuntimeValue existingValue;
    existingValue.setObjectValue(existing.getSceneObject());
    RuntimeValue freshValue;
    freshValue.setObjectValue(fresh.getSceneObject());
    const InterpreterValue existingReference(type, existingValue);
    const InterpreterValue freshReference(type, freshValue);
    const Interpreter interpreter(*virtualMachine);
    BOOST_CHECK_EXCEPTION(
        (void)interpreter.runObjectMethod(existingReference, initializer, {}),
        NomadException,
        [](const NomadException& error) {

            return NomadString(error.what()).find("field index is out of bounds") != NomadString::npos;
        }
    );
    BOOST_TEST(interpreter.runObjectMethod(freshReference, initializer, {}).getValue().getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(scene_creation_methods_preserve_queue_targets_and_callback_captures) {
    auto game = Game::createForRuntime(&runtime, options, true);
    const auto declarations = compile(
        "declareSceneRequests",
        "class SceneRequests\ncompleted = 0\n"
        "fun enqueue target:game.Scene\n"
        "target.createEntity initializeEntity 1.0 10.0 0\n"
        "target.createEntityByName \"initializeEntity\" 2.0 20.0 0\n"
        "target.createEntityByNameThen \"initializeEntity\" 3.0 30.0 0 fun\n"
        "this.completed = this.completed + 1\nend\n"
        "target.createEntityThen initializeEntity 4.0 40.0 0 fun\n"
        "this.completed = this.completed + 1\nend\n"
        "target.createTextByNameEntity \"initializeEntity\" 5.0 50.0 0 \"named text\"\n"
        "target.createTextEntity initializeEntity 6.0 60.0 0 \"callable text\"\n"
        "target.createTextEntityAndThen \"initializeEntity\" 7.0 70.0 0 \"captured text\" fun\n"
        "this.completed = this.completed + 1\nend\nend\nend\n"
        "fun initializeEntity\nthis.name = \"queued\"\nend\n"
        "fun initializeRequestScene\n"
        "requests = new SceneRequests\nglobal.sceneRequests = requests\n"
        "requests.enqueue game.currentScene\nend\n"
        "fun initializeEmptyScene\nend"
    );
    BOOST_TEST(declarations.isValid());
    runtime.initializeScripts();
    const auto firstId = game->createScene("requests", runtime.getScriptFunctionId("initializeRequestScene"));
    const auto secondId = game->createScene("empty", runtime.getScriptFunctionId("initializeEmptyScene"));
    auto polls = 0;
    game->setExitHandler({}, [&] {
        auto count = NomadIndex{0};
        if (auto* scene = game->getSceneById(firstId)) {
            scene->forEachEntities([&count](Entity*) { ++count; });
        }

        return ++polls > 16 || count == 7U;
    });
    game->run();
    auto* first = game->getSceneById(firstId);
    auto* second = game->getSceneById(secondId);
    BOOST_REQUIRE(first != nullptr);
    BOOST_REQUIRE(second != nullptr);
    std::vector<Entity*> entities;
    first->forEachEntities([&entities](Entity* entity) { entities.push_back(entity); });
    BOOST_REQUIRE(entities.size() == 7U);
    for (auto index = NomadIndex{0}; index < entities.size(); ++index) {
        BOOST_TEST(entities[index]->getX() == static_cast<NomadFloat>(index + 1));
        BOOST_TEST(entities[index]->getY() == static_cast<NomadFloat>((index + 1) * 10));
        BOOST_TEST(entities[index]->getName() == "queued");
    }

    BOOST_TEST(entities[4]->getText() == "named text");
    BOOST_TEST(entities[5]->getText() == "callable text");
    BOOST_TEST(entities[6]->getText() == "captured text");
    auto secondCount = NomadIndex{0};
    second->forEachEntities([&secondCount](Entity*) { ++secondCount; });
    BOOST_TEST(secondCount == 0U);
    RuntimeValue requestsValue;
    runtime.getContextVariableValue(runtime.getContextVariableId("global.sceneRequests"), requestsValue);
    const Interpreter interpreter(*virtualMachine);
    auto requests = interpreter.createObjectReceiver(
        *runtime.getTypeByName("SceneRequests")->asClassType(), requestsValue.getObjectValue()
    );
    BOOST_TEST(requests->getProperty("completed").getValue().getIntegerValue() == 3);
}

BOOST_AUTO_TEST_CASE(window_aliases_and_child_views_mutate_live_game_state) {
    auto game = Game::createForRuntime(&runtime, options, true);
    const auto functionId = compile(
        "updateWindow",
        "window = game.window\nmouse = window.mouse\nresolution = window.resolution\n"
        "window.setSizeAndCenter 640 360\nwindow.setResolution 320 180\n"
        "window.title = \"Property title\"\nassert window.title == \"Property title\"\n"
        "window.setTitle \"Method title\"\n"
        "window.setFps 75\nassert window.fps == 75\nwindow.fps = 90\n"
        "mouse.x = 16.0\nmouse.y = -4.0\n"
        "assert game.window.mouse.x == 16.0\nassert game.window.mouse.y == -4.0\n"
        "assert mouse.deltaX == -16.0\nassert mouse.deltaY == 4.0\n"
        "resolution.width = 400\nresolution.height = 225\n"
        "window.width = 720\nwindow.height = 405\n"
        "return $\"{window.title}:{resolution.width}x{resolution.height}\""
    );
    RuntimeValue result;
    const RuntimeValueGuard resultOwner(*runtime.getStringType(), &result);
    runtime.initializeScripts();
    virtualMachine->executeFunction(functionId, {}, result);
    BOOST_TEST(result.getStringValue() == "Method title:400x225");
    BOOST_TEST(game->getWindowSize().getX() == 720);
    BOOST_TEST(game->getWindowSize().getY() == 405);
    BOOST_TEST(game->getResolution().getX() == 400);
    BOOST_TEST(game->getResolution().getY() == 225);
    BOOST_TEST(game->getFps() == 90U);

    const Interpreter interpreter(*virtualMachine);
    InterpreterContext context(interpreter);
    BOOST_REQUIRE(interpreter.execute("window = game.window", context));
    BOOST_REQUIRE(interpreter.execute("resolution = window.resolution", context));
    game->setResolution(128, 72);
    BOOST_REQUIRE(interpreter.execute("resolution.width", context));
    BOOST_TEST(context.getResult()->getValue().getIntegerValue() == 128);
    BOOST_REQUIRE(interpreter.execute("resolution.height = 96", context));
    BOOST_TEST(game->getResolution().getY() == 96);
    BOOST_REQUIRE(interpreter.execute("window.title = \"Interpreter title\"", context));
    BOOST_REQUIRE(interpreter.execute("game.window.title", context));
    BOOST_TEST(context.getResult()->getValue().getStringValue() == "Interpreter title");
}

BOOST_AUTO_TEST_CASE(window_members_enforce_permissions_without_legacy_registrations) {
    auto game = Game::createForRuntime(&runtime, options, true);
    for (
        auto id = runtime.getFirstDynamicVariableId();
        id.isValid();
        id = runtime.getNextDynamicVariableId(id)
    ) {
        BOOST_TEST(!runtime.getDynamicVariableName(id).starts_with("window."));
    }

    for (const auto* function : runtime.getNativeFunctions()) {
        BOOST_TEST(!function->getName().starts_with("window."));
        BOOST_TEST(!function->getName().starts_with("game.window."));
    }

    const Interpreter interpreter(*virtualMachine);
    InterpreterContext context(interpreter);
    for (const auto* source : {
        "game.window = 42",
        "game.window.mouse = game.window.mouse",
        "game.window.resolution = game.window.resolution",
        "game.window.mouse.deltaX = 1.0",
        "game.window.hasFocus = true",
        "game.window.isFullScreen = true",
        "game.window.fps = 1.0",
        "new game.Window"
    }) {
        BOOST_TEST_CONTEXT(source) {
            BOOST_TEST(!interpreter.execute(source, context));
            BOOST_TEST(context.hasError());
        }
    }
}

BOOST_AUTO_TEST_CASE(retained_window_and_child_references_reject_use_after_game_destruction) {
    auto game = Game::createForRuntime(&runtime, options, true);
    const Interpreter interpreter(*virtualMachine);
    const auto constantId = runtime.getConstantId("game.window");
    RuntimeValue value;
    runtime.getConstantValue(constantId, value);
    const InterpreterValue window(runtime.getConstantType(constantId), value);
    auto windowReceiver = interpreter.createObjectReceiver(
        *window.getType()->asClassType(), window.getValue().getObjectValue()
    );
    const auto mouse = windowReceiver->getProperty("mouse");
    const auto resolution = windowReceiver->getProperty("resolution");
    auto mouseReceiver = interpreter.createObjectReceiver(
        *mouse.getType()->asClassType(), mouse.getValue().getObjectValue()
    );
    auto resolutionReceiver = interpreter.createObjectReceiver(
        *resolution.getType()->asClassType(), resolution.getValue().getObjectValue()
    );
    const auto method = runtime.resolveMethod(window.getType(), "setFps", {runtime.getIntegerType()});
    BOOST_REQUIRE(method.isResolved());
    const auto readFunctionId = compile(
        "readRetainedWindow", "params window:game.Window\nreturn window.width"
    );
    runtime.initializeScripts();
    game.reset();

    const auto reportsUnavailableWindow = [](const NomadException& error) {

        return NomadString(error.what()).find("Window is no longer available") != NomadString::npos;
    };
    BOOST_CHECK_EXCEPTION(
        (void)windowReceiver->getProperty("width"), NomadException, reportsUnavailableWindow
    );
    BOOST_CHECK_EXCEPTION(
        (void)mouseReceiver->getProperty("x"), NomadException, reportsUnavailableWindow
    );
    BOOST_CHECK_EXCEPTION(
        (void)resolutionReceiver->getProperty("width"), NomadException, reportsUnavailableWindow
    );
    const InterpreterValue fps(runtime.getIntegerType(), RuntimeValue{NomadInteger{60}});
    BOOST_CHECK_EXCEPTION(
        (void)interpreter.runObjectMethod(window, method.id, {&fps, 1}),
        NomadException,
        reportsUnavailableWindow
    );
    RuntimeValue result;
    BOOST_CHECK_EXCEPTION(
        virtualMachine->executeFunction(readFunctionId, {window.getValue()}, result),
        NomadException,
        reportsUnavailableWindow
    );
}

BOOST_AUTO_TEST_CASE(window_callbacks_capture_lexical_receivers_and_can_clear_themselves) {
    auto game = Game::createForRuntime(&runtime, options, true);
    const auto functionId = compile(
        "installWindowCallbacks",
        "class WindowEvents\nresizeWidth = 0\nresizeHeight = 0\ncloseCount = 0\n"
        "fun install\n"
        "game.window.onResize fun width height\n"
        "this.resizeWidth = width\nthis.resizeHeight = height\n"
        "game.window.clearOnResize\nend\n"
        "game.window.onClose fun\nthis.closeCount = this.closeCount + 1\n"
        "game.window.clearOnClose\nend\nend\nend\n"
        "events = new WindowEvents\nglobal.windowEvents = events\nevents.install"
    );
    runtime.initializeScripts();
    virtualMachine->executeFunction(functionId);
    auto windowCount = 0;
    auto** windows = SDL_GetWindows(&windowCount);
    BOOST_REQUIRE(windows != nullptr);
    const auto windowId = windowCount == 1 ? SDL_GetWindowID(windows[0]) : SDL_WindowID{};
    SDL_free(windows);
    BOOST_REQUIRE(windowId != 0);
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);

    for (const auto width : {320, 640}) {
        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_RESIZED;
        event.window.windowID = windowId;
        event.window.data1 = width;
        event.window.data2 = width / 2;
        BOOST_REQUIRE(SDL_PushEvent(&event));
    }

    for (auto index = 0; index < 2; ++index) {
        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.window.windowID = windowId;
        BOOST_REQUIRE(SDL_PushEvent(&event));
    }

    auto polls = 0;
    game->setExitHandler({}, [&polls] {

        return ++polls > 16;
    });
    game->run();
    BOOST_TEST(!game->running());
    const Interpreter interpreter(*virtualMachine);
    RuntimeValue eventsValue;
    runtime.getContextVariableValue(runtime.getContextVariableId("global.windowEvents"), eventsValue);
    const InterpreterValue events(runtime.getTypeByName("WindowEvents"), eventsValue);
    auto receiver = interpreter.createObjectReceiver(
        *events.getType()->asClassType(), events.getValue().getObjectValue()
    );
    BOOST_TEST(receiver->getProperty("resizeWidth").getValue().getIntegerValue() == 320);
    BOOST_TEST(receiver->getProperty("resizeHeight").getValue().getIntegerValue() == 160);
    BOOST_TEST(receiver->getProperty("closeCount").getValue().getIntegerValue() == 1);
}

BOOST_AUTO_TEST_CASE(window_properties_remain_available_in_debug_inspection) {
    options.debug = true;
    auto game = Game::createForRuntime(&runtime, options, true);
    game->onEntryExecuted();
    game->setDebugConsoleVisible(true);
    auto polls = 0;
    game->setExitHandler({}, [&polls] {

        return ++polls > 3;
    });
    BOOST_CHECK_NO_THROW(game->run());
    BOOST_TEST(polls > 3);
}

BOOST_AUTO_TEST_CASE(windowed_creation_does_not_compile_or_execute_entry)
{
    const auto functionCount = runtime.getScriptFunctionCount();
    auto game = Game::createForRuntime(&runtime, options, false);

    BOOST_TEST(game->getRuntime() == &runtime);
    BOOST_TEST(game->getResources() != nullptr);
    BOOST_TEST(game->getCanvas() != nullptr);
    BOOST_TEST(runtime.getScriptFunctionCount() == functionCount);
    BOOST_TEST(!entryExecuted);
}

BOOST_AUTO_TEST_CASE(headless_creation_does_not_compile_or_execute_entry)
{
    const auto functionCount = runtime.getScriptFunctionCount();
    options.debug = true;
    auto game = Game::createForRuntime(&runtime, options, true);

    BOOST_TEST(game->getRuntime() == &runtime);
    BOOST_TEST(runtime.isDebug());
    BOOST_TEST(runtime.getScriptFunctionCount() == functionCount);
    BOOST_TEST(!entryExecuted);
}

BOOST_AUTO_TEST_CASE(debug_console_interprets_format_literal_arithmetic)
{
    options.debug = true;
    auto game = Game::createForRuntime(&runtime, options, false);
    auto machine = runtime.createVirtualMachine();
    const Interpreter contextEvaluator(*machine);
    InterpreterContext context(contextEvaluator);

    for (const auto* source : {R"($"{1+2}")", R"(text = $"{1+2}")", "text"}) {
        BOOST_REQUIRE(context.getInterpreter().execute(source, context));
        BOOST_REQUIRE(context.getResult() != nullptr);
        if (!context.getResult()->getType()->isVoid()) {
            BOOST_TEST(context.getResult()->getType()->toString(context.getResult()->getStorage().data()) == "3");
        }
    }
}

BOOST_AUTO_TEST_CASE(script_preparation_runs_host_compilation_on_a_worker)
{
    auto game = Game::createForRuntime(&runtime, options, false);
    const auto mainThread = std::this_thread::get_id();
    std::thread::id compilationThread;
    game->prepareScripts([this, &compilationThread] {
        compilationThread = std::this_thread::get_id();
        compile("prepared", "return 17\n");
    });

    BOOST_TEST(compilationThread != mainThread);
    RuntimeValue result;
    virtualMachine->executeFunction(runtime.getScriptFunctionId("prepared"), {}, result);
    BOOST_TEST(result.getIntegerValue() == 17);
    BOOST_TEST(!entryExecuted);
}

BOOST_AUTO_TEST_CASE(sprite_loading_is_deferred_until_script_preparation)
{
    directory.write("sprites/broken.png", "not a PNG");
    options.sprites.directories = std::vector<NomadPath>{"sprites"};
    auto game = Game::createForRuntime(&runtime, options, false);
    auto compiled = false;

    BOOST_CHECK_THROW(
        game->prepareScripts([&compiled] {
            compiled = true;
        }),
        SpritePackerException
    );
    BOOST_TEST(compiled);
}

BOOST_AUTO_TEST_CASE(script_preparation_propagates_host_compilation_failure)
{
    auto game = Game::createForRuntime(&runtime, options, false);

    BOOST_CHECK_EXCEPTION(
        game->prepareScripts([] {
            throw std::runtime_error("Host compilation failed");
        }),
        std::runtime_error,
        [](const std::runtime_error& error) {
            return NomadString(error.what()) == "Host compilation failed";
        }
    );
}

BOOST_AUTO_TEST_CASE(game_executes_its_entry_and_releases_script_state)
{
    options.entryFunction = "init";
    auto game = Game::createForRuntime(&runtime, options, true);
    game->executeEntry();
    game->onEntryExecuted();

    BOOST_TEST(entryExecuted);
    game->releaseScriptState();
    game->releaseScriptState();
    BOOST_TEST(game->getRuntime() == &runtime);
    BOOST_TEST(!game->running());
}

BOOST_AUTO_TEST_CASE(exit_poll_stops_run_before_processing_a_frame)
{
    auto game = Game::createForRuntime(&runtime, options, true);
    auto pollCount = 0;
    auto requestCount = 0;
    game->setExitHandler(
        [&requestCount] {
            ++requestCount;
        },
        [&pollCount] {
            ++pollCount;
            return true;
        }
    );

    game->run();

    BOOST_TEST(pollCount == 1);
    BOOST_TEST(requestCount == 0);
    BOOST_TEST(!game->running());
}

BOOST_AUTO_TEST_CASE(quit_stops_game_before_notifying_host)
{
    auto game = Game::createForRuntime(&runtime, options, true);
    auto requested = false;
    game->setExitHandler(
        [&game, &requested] {
            BOOST_TEST(!game->running());
            if (!requested) {
                requested = true;
                game->quit();
            }
        },
        {}
    );

    game->quit();

    BOOST_TEST(requested);
    BOOST_TEST(!game->running());
}

BOOST_AUTO_TEST_SUITE_END()
