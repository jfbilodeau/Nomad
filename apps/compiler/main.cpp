// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/game/Game.hpp>
#include <nomad/log/Logger.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

using namespace nomad;

namespace {

void printUsage(std::ostream& out) {
    out
        << "Usage:\n"
        << "  nomadc check [path]\n"
        << "  nomadc dump [path] [--function <name>] [--format text]\n"
        << "  nomadc docs [path] --format markdown --output <file>\n";
}

void printDiagnostics(const CompilationResult& result) {
    for (const auto& diagnostic : result.diagnostics) {
        std::cerr << formatDiagnostic(diagnostic) << '\n';
    }
}

// Stands up the real engine against SDL's dummy drivers so compiled sources resolve the same
// `game.*`, `window.*`, `scene.*` and `t.*` symbols a windowed build would expose.
class HeadlessGame {
public:
    explicit HeadlessGame(const std::filesystem::path& path) {
        auto resourcePath = std::filesystem::is_regular_file(path) ? path.parent_path() : path;

        if (resourcePath.empty()) {
            resourcePath = std::filesystem::current_path();
        }

        m_options.resourcePath = resourcePath.generic_string();
        m_game = std::make_unique<Game>(&m_options);
        m_game->initializeHeadless();
    }

    [[nodiscard]] Runtime* getRuntime() const { return m_game->getRuntime(); }

private:
    GameOptions m_options;
    std::unique_ptr<Game> m_game;
};

} // namespace

int main(const int argc, char** argv) {
    log::setLogLevel(LogLevel::Warning);
    log::clear();

    if (argc < 2) {
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }

    const NomadStringView command(argv[1]);

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
        const HeadlessGame game(path);
        const auto result = checkPath(path, game.getRuntime());
        printDiagnostics(result);

        return result.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (command == "dump") {
        auto path = std::filesystem::current_path();
        std::optional<NomadString> functionName;
        auto pathSet = false;

        for (auto index = 2; index < argc; ++index) {
            const NomadStringView argument(argv[index]);

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

                if (NomadStringView(argv[index]) != "text") {
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

        const HeadlessGame game(path);
        const auto result = dumpInstructions(path, functionName, game.getRuntime());
        printDiagnostics(result.compilation);

        if (!result.compilation.succeeded()) {
            return EXIT_FAILURE;
        }

        std::cout << result.instructions;
        return EXIT_SUCCESS;
    }

    if (command == "docs") {
        auto path = std::filesystem::current_path();
        std::optional<std::filesystem::path> outputPath;
        auto pathSet = false;

        for (auto index = 2; index < argc; ++index) {
            const NomadStringView argument(argv[index]);

            if (argument == "--format") {
                if (++index >= argc) {
                    std::cerr << "The --format option requires a format\n";
                    return EXIT_FAILURE;
                }

                if (NomadStringView(argv[index]) != "markdown") {
                    std::cerr << "The docs command currently supports only Markdown output\n";
                    return EXIT_FAILURE;
                }
            } else if (argument == "--output") {
                if (++index >= argc) {
                    std::cerr << "The --output option requires a file\n";
                    return EXIT_FAILURE;
                }

                outputPath = argv[index];
            } else if (!pathSet) {
                path = argv[index];
                pathSet = true;
            } else {
                std::cerr << "Unexpected docs argument: " << argument << '\n';
                printUsage(std::cerr);
                return EXIT_FAILURE;
            }
        }

        if (!outputPath) {
            std::cerr << "The docs command requires --output <file>\n";
            return EXIT_FAILURE;
        }

        const HeadlessGame game(path);
        const auto result = generateDocumentationForPath(path, game.getRuntime());
        printDiagnostics(result.compilation);

        if (!result.compilation.succeeded()) {
            return EXIT_FAILURE;
        }

        std::ofstream output(*outputPath, std::ios::binary);

        if (!output.is_open()) {
            std::cerr << "Failed to open documentation output: " << outputPath->string() << '\n';
            return EXIT_FAILURE;
        }

        output << result.documentation;

        if (!output) {
            std::cerr << "Failed to write documentation output: " << outputPath->string() << '\n';
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    }

    {
        std::cerr << "Unknown command: " << command << '\n';
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }
}
