// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compiler.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/resource/SpritePacker.hpp>

#include <nomad/script/Runtime.hpp>
#include <nomad/script/Interpreter.hpp>

#include <TestDirectory.hpp>

#include <boost/test/unit_test.hpp>

#include <SDL3/SDL.h>

#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace nomad;
using namespace nomad::test;

namespace {

struct ModuleGameFixture {
    TestDirectory directory{"nomad_module_game"};
    bool entryExecuted = false;
    Runtime runtime;
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

    NomadId compile(const NomadString& name, const NomadString& source) {
        auto compiler = runtime.createCompiler();
        CompilerContext context(compiler.get());
        compiler->registerSources(&context, {{{name, {name + ".nomad", source}}}, {}});
        BOOST_REQUIRE(compiler->compileFunctions(&context));
        return runtime.getFunctionId(name);
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(module_game, ModuleGameFixture)

BOOST_AUTO_TEST_CASE(borrowed_runtime_remains_usable_after_game_destruction)
{
    auto game = Game::createForRuntime(&runtime, options, true);
    BOOST_TEST(game->getRuntime() == &runtime);
    game.reset();

    const auto functionId = compile("afterGame", "return 42\n");
    RuntimeValue result;
    runtime.executeFunction(functionId, {}, result);
    BOOST_TEST(result.getIntegerValue() == 42);
}

BOOST_AUTO_TEST_CASE(windowed_creation_does_not_compile_or_execute_entry)
{
    const auto functionCount = runtime.getFunctionCount();
    auto game = Game::createForRuntime(&runtime, options, false);

    BOOST_TEST(game->getRuntime() == &runtime);
    BOOST_TEST(game->getResources() != nullptr);
    BOOST_TEST(game->getCanvas() != nullptr);
    BOOST_TEST(runtime.getFunctionCount() == functionCount);
    BOOST_TEST(!entryExecuted);
}

BOOST_AUTO_TEST_CASE(headless_creation_does_not_compile_or_execute_entry)
{
    const auto functionCount = runtime.getFunctionCount();
    options.debug = true;
    auto game = Game::createForRuntime(&runtime, options, true);

    BOOST_TEST(game->getRuntime() == &runtime);
    BOOST_TEST(runtime.isDebug());
    BOOST_TEST(runtime.getFunctionCount() == functionCount);
    BOOST_TEST(!entryExecuted);
}

BOOST_AUTO_TEST_CASE(debug_console_interprets_format_literal_arithmetic)
{
    options.debug = true;
    auto game = Game::createForRuntime(&runtime, options, false);
    auto machine = runtime.createVirtualMachine();
    InterpreterContext context(*machine);

    for (const auto* source : {R"($"{1+2}")", R"(text = $"{1+2}")", "text"}) {
        BOOST_REQUIRE(Interpreter{}.execute(source, context));
        BOOST_REQUIRE(context.getResult() != nullptr);
        if (!context.getResult()->getType()->isVoid()) {
            BOOST_TEST(context.getResult()->getType()->toString(context.getResult()->getValue()) == "3");
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
    runtime.executeFunction(runtime.getFunctionId("prepared"), {}, result);
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

BOOST_AUTO_TEST_CASE(host_executes_entry_and_releases_script_state)
{
    auto game = Game::createForRuntime(&runtime, options, true);
    runtime.executeFunction(runtime.getFunctionId("init"));
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
