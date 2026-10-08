// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerTools.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/system/Path.hpp>

#include <nomad/Version.hpp>

#include <boost/nowide/args.hpp>
#include <boost/program_options.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <utility>

using namespace nomad;

namespace {

namespace po = boost::program_options;

void printUsage(std::ostream& output, const po::options_description& options) {
    output << options << '\n';
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
        options.resourcePath = pathToUtf8(resourcePath);
        m_game = Game::createHeadless(std::move(options));
    }

    [[nodiscard]] Runtime* getRuntime() const { return m_game->getRuntime(); }

private:
    std::unique_ptr<Game> m_game;
};

CompilationPaths resolveRequestedPaths(const po::variables_map& arguments) {
    const auto explicitPath = arguments.contains("path");
    const auto path = explicitPath
        ? pathFromUtf8(arguments["path"].as<NomadString>())
        : std::filesystem::current_path();

    return resolveCompilationPaths(path, explicitPath);
}

int versionCommand() {
    std::cout << "nomadc " << getNomadVersion() << '\n';
    return EXIT_SUCCESS;
}

int checkCommand(const po::variables_map& arguments) {
    const auto paths = resolveRequestedPaths(arguments);
    const HeadlessGame game(paths.resources);
    auto result = checkPaths(paths.sources, game.getRuntime());

    if (paths.entryFunction) {
        validateEntryFunction(result, game.getRuntime(), *paths.entryFunction, paths.projectFile);
    }

    printDiagnostics(result);
    return result.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
}

int dumpCommand(const po::variables_map& arguments) {
    std::optional<NomadString> functionName;

    if (arguments.contains("function")) {
        functionName = arguments["function"].as<NomadString>();
    }

    const auto paths = resolveRequestedPaths(arguments);
    const HeadlessGame game(paths.resources);
    const auto result = dumpInstructions(paths.sources, functionName, game.getRuntime());
    printDiagnostics(result.compilation);

    if (!result.compilation.succeeded()) {
        return EXIT_FAILURE;
    }

    std::cout << result.instructions;
    return EXIT_SUCCESS;
}

int documentationCommand(const po::variables_map& arguments) {
    const auto outputPath = pathFromUtf8(arguments["output"].as<NomadString>());
    const auto paths = resolveRequestedPaths(arguments);
    const HeadlessGame game(paths.resources);
    const auto result = generateDocumentationForPaths(paths.sources, game.getRuntime());
    printDiagnostics(result.compilation);

    if (!result.compilation.succeeded()) {
        return EXIT_FAILURE;
    }

    std::ofstream output(outputPath, std::ios::binary);

    if (!output.is_open()) {
        std::cerr << "Failed to open documentation output: " << pathToUtf8(outputPath) << '\n';
        return EXIT_FAILURE;
    }

    output << result.documentation;

    if (!output) {
        std::cerr << "Failed to write documentation output: " << pathToUtf8(outputPath) << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

// Boost.ProgramOptions parses every option globally, so each command rejects the
// options it does not implement instead of silently ignoring them.
void validateCommandOptions(const NomadStringView command, const po::variables_map& arguments) {
    if (arguments.contains("function") && command != "dump") {
        throw po::error("The --function option is supported only by the dump command");
    }

    if (arguments.contains("output") && command != "docs") {
        throw po::error("The --output option is supported only by the docs command");
    }

    if (command == "docs" && !arguments.contains("output")) {
        throw po::error("The docs command requires --output <file>");
    }

    if (command == "version" && arguments.contains("path")) {
        throw po::error("The version command does not accept arguments");
    }
}

int dispatchCommand(
    const NomadStringView command,
    const po::variables_map& arguments,
    const po::options_description& visibleOptions
) {
    validateCommandOptions(command, arguments);

    if (command == "version") {
        return versionCommand();
    }

    if (command == "check") {
        return checkCommand(arguments);
    }

    if (command == "dump") {
        return dumpCommand(arguments);
    }

    if (command == "docs") {
        return documentationCommand(arguments);
    }

    std::cerr << "Unknown command: " << command << '\n';
    printUsage(std::cerr, visibleOptions);
    return EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv) {
    boost::nowide::args utf8Arguments(argc, argv);

    log::setLogLevel(LogLevel::Warning);
    log::clear();

    po::options_description visibleOptions(
        "Usage:\n"
        "  nomadc check [path]\n"
        "  nomadc dump [path] [--function <name>]\n"
        "  nomadc docs [path] --output <file>\n"
        "  nomadc version\n"
        "\n"
        "Options"
    );
    visibleOptions.add_options()
        ("help,h", "Show this help")
        ("version,v", "Show the Nomad version")
        ("function", po::value<NomadString>(), "Function to dump")
        ("output", po::value<NomadString>(), "Output file");

    po::options_description hiddenOptions;
    hiddenOptions.add_options()
        ("command", po::value<NomadString>(), "Command to run")
        ("path", po::value<NomadString>(), "Source or project path");

    po::options_description allOptions;
    allOptions.add(visibleOptions).add(hiddenOptions);

    po::positional_options_description positional;
    positional.add("command", 1);
    positional.add("path", 1);

    try {
        po::variables_map arguments;
        po::store(
            po::command_line_parser(argc, argv)
                .options(allOptions)
                .positional(positional)
                .run(),
            arguments
        );
        po::notify(arguments);

        if (arguments.contains("help")) {
            printUsage(std::cout, visibleOptions);
            return EXIT_SUCCESS;
        }

        if (arguments.contains("version")) {
            if (arguments.contains("command")) {
                throw po::error("The version option does not accept arguments");
            }

            return dispatchCommand("version", arguments, visibleOptions);
        }

        if (!arguments.contains("command")) {
            printUsage(std::cerr, visibleOptions);
            return EXIT_FAILURE;
        }

        return dispatchCommand(arguments["command"].as<NomadString>(), arguments, visibleOptions);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
