// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/Module.hpp>

#include <nomad/game/Game.hpp>
#include <nomad/game/GamePackaging.hpp>
#include <nomad/game/GameResourceCheck.hpp>

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/system/Path.hpp>

#include <memory>
#include <utility>

NOMAD_MODULE_EXPORT void registerModule(nomad::ModuleContext* context, nomad::ModuleCallbacks* callbacks) noexcept(false) {
    callbacks->package = [](nomad::ModulePackageContext& package) {
        nomad::packageGameResources(&package);
    };
    if (context->mode == nomad::ModuleMode::Packaging) {
        return;
    }
    auto options = nomad::readGameOptions(*context->configuration);
    if (!options.title) {
        options.title = context->configuration->getString("project.name");
    }
    options.resourcePath = nomad::pathToUtf8(context->resourceDirectory);
    options.audio.missingDirectoriesAllowed = !context->configuration->contains("project.executable");
    options.sprites.missingDirectoriesAllowed = options.audio.missingDirectoriesAllowed;
    auto hasDebug = false;
    auto hasNoDebug = false;
    for (const auto& argument : context->arguments) {
        hasDebug = hasDebug || argument == "--debug";
        hasNoDebug = hasNoDebug || argument == "--no-debug";
    }
    if (hasDebug && hasNoDebug) {
        throw nomad::GameException("--debug and --no-debug cannot be used together");
    }
    if (hasDebug || hasNoDebug) {
        options.debug = hasDebug;
    }
    auto game = std::shared_ptr<nomad::Game>(nomad::Game::createForRuntime(
        context->runtime, std::move(options), context->mode != nomad::ModuleMode::Application
    ));
    game->setExitHandler(
        [requestExit = context->requestExit] { requestExit(0); }, context->pollExit
    );
    callbacks->shutdown = [game] { game->releaseScriptState(); };
    callbacks->checkResources = [context] { return nomad::checkGameResources(*context); };
    if (context->mode == nomad::ModuleMode::Application) {
        callbacks->prepareCompilation = [game, context] { game->prepareScripts(context->compileScripts); };
        callbacks->run = [game] {
            game->onEntryExecuted();
            game->run();
            return 0;
        };
        callbacks->exit = [game] { game->quit(); };
    }
}
