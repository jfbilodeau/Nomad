// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/system/Module.hpp>

#include <nomad/game/Game.hpp>
#include <nomad/game/GameConfiguration.hpp>
#include <nomad/game/GamePackaging.hpp>
#include <nomad/game/GameResourceCheck.hpp>

#include <nomad/script/Runtime.hpp>

#include <nomad/system/ModuleManifest.hpp>
#include <nomad/system/Path.hpp>

#include <memory>

namespace {

void packageModule(nomad::ModulePackageContext& context) {
    context.resourceDirectory = nomad::resolveGameResourcePath(*context.source, context.projectDirectory);
    const auto output = context.stagingDirectory.parent_path();
    const auto within = [](const nomad::NomadPath& path, const nomad::NomadPath& parent) {
        const auto relative = path.lexically_normal().lexically_relative(parent.lexically_normal());
        return !relative.empty() && *relative.begin() != "..";
    };
    if (within(output, context.resourceDirectory) || within(context.resourceDirectory, output)) {
        throw nomad::ModuleException("Package output must not contain or replace the project resources");
    }
    nomad::normalizeGameConfiguration(*context.source, *context.release);
    nomad::packageGameResources(&context);
    context.copyDirectory(context.resourceDirectory, "res");
}

nomad::GameOptions getGameOptions(const nomad::ModuleContext& context) {
    auto options = nomad::readGameOptions(*context.configuration);
    options.configureDebugLogLevel = !context.runtime->isHub("log") || !context.configuration->contains("log.level");

    if (!options.title || options.title->empty()) {
        options.title = "Nomad";
    }

    options.resourcePath = nomad::pathToUtf8(
        nomad::resolveGameResourcePath(*context.configuration, context.projectDirectory)
    );

    options.audio.missingDirectoriesAllowed = !context.configuration->contains("project.executable");
    options.sprites.missingDirectoriesAllowed = options.audio.missingDirectoriesAllowed;
    auto hasDebug = false;
    auto hasNoDebug = false;
    for (const auto& argument : context.arguments) {
        if (argument == "--") {
            break;
        }
        hasDebug = hasDebug || argument == "--debug";
        hasNoDebug = hasNoDebug || argument == "--no-debug";
    }
    if (hasDebug && hasNoDebug) {
        throw nomad::GameException("--debug and --no-debug cannot be used together");
    }
    if (hasDebug || hasNoDebug) {
        options.debug = hasDebug;
    }
    return options;
}

std::shared_ptr<nomad::Game> createGame(const nomad::ModuleContext& context) {
    auto game = std::shared_ptr<nomad::Game>(nomad::Game::createForRuntime(
        context.runtime, getGameOptions(context), context.mode != nomad::ModuleMode::Application
    ));
    game->setExitHandler(
        [requestExit = context.requestExit] { requestExit(0); }, context.pollExit
    );
    return game;
}

int runModule(nomad::Game& game) {
    game.executeEntry();
    game.onEntryExecuted();
    game.run();
    return 0;
}

} // namespace

NOMAD_MODULE_EXPORT void registerModule(nomad::ModuleContext* context, nomad::ModuleCallbacks* callbacks) noexcept(false) {
    callbacks->package = packageModule;
    if (context->mode == nomad::ModuleMode::Packaging) {
        return;
    }
    const auto game = createGame(*context);
    callbacks->shutdown = [game] { game->releaseScriptState(); };
    callbacks->checkResources = [context] { return nomad::checkGameResources(*context); };
    if (context->mode == nomad::ModuleMode::Application) {
        callbacks->prepareCompilation = [game, context] { game->prepareScripts(context->compileScripts); };
        callbacks->run = [game] { return runModule(*game); };
        callbacks->exit = [game] { game->quit(); };
    }
}
