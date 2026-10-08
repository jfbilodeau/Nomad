// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerTools.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/Version.hpp>

#include <boost/nowide/args.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <utility>

using namespace nomad;

namespace {

void printUsage(std::ostream& out) {
    out
        << "Usage:\n"
        << "  nomadc check [path]\n"
        << "  nomadc dump [path] [--function <name>] [--format text]\n"
        << "  nomadc docs [path] --format markdown --output <file>\n"
        << "  nomadc version\n";
}

void printDiagnostics(const CompilationResult& result) {
    for (const auto& diagnostic : result.diagnostics) {
        std::cerr << formatDiagnostic(diagnostic) << '\n';
    }
}

struct CompilationPaths {
    std::vector<NomadPath> sources;
    NomadPath resources;
    std::optional<NomadString> entryFunction;
    NomadPath projectFile;
};

CompilationPaths resolveCompilationPaths(const NomadPath& requestedPath, const bool explicitPath) {
    auto resourcePath = std::filesystem::is_regular_file(requestedPath)
        ? requestedPath.parent_path()
        : requestedPath;
    std::vector<NomadPath> sourcePaths{requestedPath};

    if (std::filesystem::exists(requestedPath)) {
        const auto projectRoot = findProjectRoot(requestedPath);

        if (projectRoot) {
            const auto configuration = loadProjectConfiguration(*projectRoot / NOMAD_PROJECT_FILE_NAME);
            resourcePath = resolveProjectResourcePath(configuration);

            if (!explicitPath) {
                sourcePaths = {resourcePath / "scripts"};
                const auto modsPath = resourcePath / "mods";

                if (std::filesystem::is_directory(modsPath)) {
                    sourcePaths.push_back(modsPath);
                }

                return CompilationPaths{
                    std::move(sourcePaths),
                    resourcePath,
                    configuration.project.entry,
                    *projectRoot / NOMAD_PROJECT_FILE_NAME
                };
            }
        }
    }

    if (resourcePath.empty()) {
        resourcePath = std::filesystem::current_path();
    }

    return CompilationPaths{std::move(sourcePaths), resourcePath, std::nullopt, {}};
}

// Stands up the real engine against SDL's dummy drivers so compiled sources resolve the same
// `game.*`, `window.*`, `scene.*` and `t.*` symbols a windowed build would expose.
class HeadlessGame {
public:
    explicit HeadlessGame(const NomadPath& resourcePath) {
        GameOptions options;
        options.resourcePath = pathToString(resourcePath);
        m_game = Game::createHeadless(std::move(options));
    }

    [[nodiscard]] Runtime* getRuntime() const { return m_game->getRuntime(); }

private:
    std::unique_ptr<Game> m_game;
};

} // namespace

int main(int argc, char** argv) {
    boost::nowide::args utf8Arguments(argc, argv);

    log::setLogLevel(LogLevel::Warning);
    log::clear();

    try {
    if (argc < 2) {
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }

    const NomadStringView command(argv[1]);

    if (command == "--help" || command == "-h") {
        printUsage(std::cout);
        return EXIT_SUCCESS;
    }

    if (command == "--version" || command == "version") {
        if (argc != 2) {
            std::cerr << "The version command does not accept arguments\n";
            return EXIT_FAILURE;
        }

        std::cout << "nomadc " << getNomadVersion() << '\n';
        return EXIT_SUCCESS;
    }

    if (command == "check") {
        if (argc > 3) {
            std::cerr << "The check command accepts at most one path\n";
            printUsage(std::cerr);
            return EXIT_FAILURE;
        }

        const auto explicitPath = argc == 3;
        const auto path = explicitPath ? pathFromString(argv[2]) : std::filesystem::current_path();
        const auto paths = resolveCompilationPaths(path, explicitPath);
        const HeadlessGame game(paths.resources);
        auto result = checkPaths(paths.sources, game.getRuntime());

        if (paths.entryFunction) {
            validateEntryFunction(result, game.getRuntime(), *paths.entryFunction, paths.projectFile);
        }

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
                path = pathFromString(argv[index]);
                pathSet = true;
            } else {
                std::cerr << "Unexpected dump argument: " << argument << '\n';
                printUsage(std::cerr);
                return EXIT_FAILURE;
            }
        }

        const auto paths = resolveCompilationPaths(path, pathSet);
        const HeadlessGame game(paths.resources);
        const auto result = dumpInstructions(paths.sources, functionName, game.getRuntime());
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

                outputPath = pathFromString(argv[index]);
            } else if (!pathSet) {
                path = pathFromString(argv[index]);
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

        const auto paths = resolveCompilationPaths(path, pathSet);
        const HeadlessGame game(paths.resources);
        const auto result = generateDocumentationForPaths(paths.sources, game.getRuntime());
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
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
