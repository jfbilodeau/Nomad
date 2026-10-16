// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/GameFactory.hpp>

#include <nomad/log/ConsoleSink.hpp>
#include <nomad/log/Logger.hpp>

#include <nomad/game/Game.hpp>
#include <nomad/project/ProjectConfiguration.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/Version.hpp>

#define BOOST_NO_CXX98_FUNCTION_BASE
#include <boost/program_options.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <utility>

namespace nomad
{

void parseCommandLine(const int argc, char **argv, GameOptions *options)
{
    boost::program_options::options_description desc("Allowed options");

    desc.add_options()
        ("help", "Display help and exit")
        ("debug", "Enable debug mode")
        ("no-debug", "Disable debug mode")
        ("resource-path", boost::program_options::value<NomadString>(&options->resourcePath),
         "Path to resource directory");

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.contains("help")) {
        std::cout << desc << std::endl;
        std::exit(EXIT_SUCCESS);
    }

    if (vm.contains("debug") && vm.contains("no-debug")) {
        throw boost::program_options::error("--debug and --no-debug cannot be used together");
    }

    if (vm.contains("debug")) {
        log::info("Debug mode enabled");
        log::setLogLevel(LogLevel::Debug);
        options->debug = true;
    }
    else if (vm.contains("no-debug")) {
        log::setLogLevel(LogLevel::Info);
        options->debug = false;
    }
    else {
        log::setLogLevel(LogLevel::Info);
    }

}

void loadProjectOptions(const NomadPath& startPath, GameOptions* options) {
    auto project = discoverNomadProject(startPath);
    validateNomadVersionCompatibility(project.configuration.sdk.version, getNomadVersion());
    const auto resourcePathOverride = std::move(options->resourcePath);
    const auto debugOverride = options->debug;
    *options = std::move(project.gameOptions);

    if (!resourcePathOverride.empty()) {
        options->resourcePath = resourcePathOverride;
    }
    if (debugOverride) {
        options->debug = debugOverride;
    }
    if (!options->title) {
        options->title = project.configuration.project.name;
    }

    std::error_code error;
    const auto resourcePath = pathFromUtf8(options->resourcePath);

    if (!std::filesystem::is_directory(resourcePath, error)) {
        auto message = "Resource directory does not exist: '" + pathToUtf8(resourcePath) + "'";

        if (error && error != std::errc::no_such_file_or_directory) {
            message += ": " + error.message();
        }

        throw ProjectConfigurationError(message);
    }
}

int run(const int argc, char **argv) {
    // ConsoleSink consoleSink;
    // const auto logger = std::make_shared<Logger>(&consoleSink);

    try {
        GameOptions options;

        parseCommandLine(argc, argv, &options);
        loadProjectOptions(std::filesystem::current_path(), &options);

        auto game = Game::create(std::move(options));

        try {
            game->run();
        }
        catch (const NomadException &e) {
            SDL_ShowSimpleMessageBox((SDL_MESSAGEBOX_ERROR), "Internal Error", e.what(), nullptr);
            log::error(e.what());
            return EXIT_FAILURE;
        }
        catch (const std::bad_alloc &e) {
            SDL_ShowSimpleMessageBox((SDL_MESSAGEBOX_ERROR), "Out of memory", e.what(), nullptr);
            log::error(e.what());
            return EXIT_FAILURE;
        }
    }
    catch (std::exception &e) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", e.what(), nullptr);
        log::error(e.what());

        return EXIT_FAILURE;
    }
    catch (...) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "An unknown error occurred", nullptr);
        log::error("Unknown error");

        return EXIT_FAILURE;
    }

    log::info("Shutting down Nomad");

    // log::debug("Removing console sink");
    // log::removeSink(&consoleSink);

    log::flush();

    return EXIT_SUCCESS;
}

} // nomad
