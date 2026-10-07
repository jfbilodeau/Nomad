// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/log/Logger.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void printUsage(std::ostream& out) {
    out
        << "Usage:\n"
        << "  nomadc check [path]\n"
        << "  nomadc dump [path] [--function <name>] [--format text]\n";
}

void printDiagnostics(const nomad::CompilationResult& result) {
    for (const auto& diagnostic : result.diagnostics) {
        std::cerr << nomad::formatDiagnostic(diagnostic) << '\n';
    }
}

} // namespace

int main(const int argc, char** argv) {
    nomad::log::setLogLevel(nomad::LogLevel::Warning);
    nomad::log::clear();

    if (argc < 2) {
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }

    const std::string_view command(argv[1]);

    if (command == "--help" || command == "-h") {
        printUsage(std::cout);
        return EXIT_SUCCESS;
    }

    if (command == "check") {
        if (argc > 3) {
            std::cerr << "The check command accepts at most one path\n";
            printUsage(std::cerr);
            return EXIT_FAILURE;
        }

        const auto path = argc == 3 ? std::filesystem::path(argv[2]) : std::filesystem::current_path();
        const auto result = nomad::checkPath(path);
        printDiagnostics(result);

        return result.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (command == "dump") {
        auto path = std::filesystem::current_path();
        std::optional<nomad::NomadString> functionName;
        auto pathSet = false;

        for (auto index = 2; index < argc; ++index) {
            const std::string_view argument(argv[index]);

            if (argument == "--function") {
                if (++index >= argc) {
                    std::cerr << "The --function option requires a function name\n";
                    return EXIT_FAILURE;
                }

                functionName = argv[index];
            } else if (argument == "--format") {
                if (++index >= argc) {
                    std::cerr << "The --format option requires a format\n";
                    return EXIT_FAILURE;
                }

                if (std::string_view(argv[index]) != "text") {
                    std::cerr << "The dump command currently supports only text output\n";
                    return EXIT_FAILURE;
                }
            } else if (!pathSet) {
                path = argv[index];
                pathSet = true;
            } else {
                std::cerr << "Unexpected dump argument: " << argument << '\n';
                printUsage(std::cerr);
                return EXIT_FAILURE;
            }
        }

        const auto result = nomad::dumpInstructions(path, functionName);
        printDiagnostics(result.compilation);

        if (!result.compilation.succeeded()) {
            return EXIT_FAILURE;
        }

        std::cout << result.instructions;
        return EXIT_SUCCESS;
    }

    {
        std::cerr << "Unknown command: " << command << '\n';
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }
}
