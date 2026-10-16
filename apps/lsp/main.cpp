// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/Logger.hpp>

#include <nomad/lsp/LanguageServer.hpp>

#include <nomad/Version.hpp>

#include <boost/nowide/args.hpp>
#include <boost/program_options.hpp>

#include <iostream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char** argv) {
    boost::nowide::args utf8Arguments(argc, argv);
    namespace options = boost::program_options;
    options::options_description description("Nomad language server options");
    description.add_options()
        ("help,h", "Show help")
        ("stdio", "Use standard input/output transport (default)")
        ("version,v", "Show the Nomad version");
    try {
        options::variables_map parameters;
        options::store(options::command_line_parser(argc, argv).options(description)
            .style(options::command_line_style::default_style & ~options::command_line_style::allow_guessing)
            .run(), parameters);
        options::notify(parameters);
        if (parameters.count("help") != 0) {
            std::cout << "Usage: nomad-lsp [options]\n"
                "Runs the Nomad diagnostics language server over standard input/output.\n\n"
                << description;
            return 0;
        }
        if (parameters.count("version") != 0) {
            std::cout << "nomad-lsp " << nomad::getNomadVersion() << '\n';
            return 0;
        }
    } catch (const options::error& error) {
        std::cerr << error.what() << "\nUsage: nomad-lsp [options]\n" << description;
        return 1;
    }
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::cerr << "Failed to configure language server binary streams\n";
        return 1;
    }
#endif
    nomad::log::setLogLevel(nomad::LogLevel::Warning);
    nomad::log::clear();
    // SDK console output must never corrupt the protocol stream.
    std::ostream protocol(std::cout.rdbuf());
    struct ConsoleRedirection {
        std::streambuf* previous = std::cout.rdbuf(std::cerr.rdbuf());
        ~ConsoleRedirection() { std::cout.rdbuf(previous); }
    };
    const ConsoleRedirection redirection;
    return nomad::runLanguageServer(std::cin, protocol, std::cerr, nomad::analyzeLanguageDocuments);
}
