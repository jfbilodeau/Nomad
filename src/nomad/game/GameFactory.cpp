// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/GameFactory.hpp>

#include <nomad/log/ConsoleSink.hpp>
#include <nomad/log/Logger.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/script/Documentation.hpp>

#define BOOST_NO_CXX98_FUNCTION_BASE
#include <boost/program_options.hpp>

#include <cstdlib>
#include <iostream>
#include <fstream>

namespace nomad
{

void parseCommandLine(const int argc, char **argv, GameOptions *options)
{
    boost::program_options::options_description desc("Allowed options");

    desc.add_options()
        ("help", "Display help and exit")
        ("debug", "Enable debug mode")
        ("doc", "Generate function documentation")
        ("tm", "Generate tmLanguage syntax highlighting files")
        ("resource-path", boost::program_options::value<NomadString>(&options->resourcePath),
         "Path to resource directory");

    boost::program_options::variables_map vm;
    boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);
    boost::program_options::notify(vm);

    if (vm.contains("help")) {
        std::cout << desc << std::endl;
        std::exit(EXIT_SUCCESS);
    }

    if (vm.contains("doc")) {
        options->generateDocumentation = true;
    }
    if (vm.contains("tm")) {
        options->generateTextMateGrammar = true;
    }
    if (vm.contains("debug")) {
        log::info("Debug mode enabled");
        log::setLogLevel(LogLevel::Debug);
        options->debug = true;
    }
    else {
        log::setLogLevel(LogLevel::Info);
        options->debug = false;
    }

    if (options->resourcePath.empty()) {
        options->resourcePath = NomadString(SDL_GetBasePath());
    }
}

int run(const int argc, char **argv) {
    // ConsoleSink consoleSink;
    // const auto logger = std::make_shared<Logger>(&consoleSink);

    try {
        GameOptions options;

        parseCommandLine(argc, argv, &options);

        Game game(&options);
        game.initialize();

        try {
            game.run();
        }
        catch (const NomadException &e) {
            SDL_ShowSimpleMessageBox((SDL_MESSAGEBOX_ERROR), "Internal Error", e.what(), nullptr);
            log::fatal(e.what());
        }
        catch (const std::bad_alloc &e) {
            SDL_ShowSimpleMessageBox((SDL_MESSAGEBOX_ERROR), "Out of memory", e.what(), nullptr);
            log::fatal(e.what());
        }
    }
    catch (std::exception &e) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", e.what(), nullptr);
        log::fatal(e.what());

        return EXIT_FAILURE;
    }
    catch (...) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "An unknown error occurred", nullptr);
        log::fatal("Unknown error");

        return EXIT_FAILURE;
    }

    log::info("Shutting down Nomad");

    // log::debug("Removing console sink");
    // log::removeSink(&consoleSink);

    log::flush();

    return EXIT_SUCCESS;
}

} // nomad
