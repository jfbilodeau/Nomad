// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/CompilerTools.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/ProgramOptions.hpp>

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

struct CheckParameters {
    std::optional<NomadString> path;
};

struct DumpParameters {
    std::optional<NomadString> path;
    std::optional<NomadString> function;
};

struct DocumentationParameters {
    std::optional<NomadString> path;
    NomadString output;
};

struct HelpParameters {
    std::optional<NomadString> verb;
};

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

CompilationPaths resolveRequestedPaths(const std::optional<NomadString>& requestedPath) {
    const auto explicitPath = requestedPath.has_value();
    const auto path = explicitPath
        ? pathFromUtf8(*requestedPath)
        : std::filesystem::current_path();

    return resolveCompilationPaths(path, explicitPath);
}

int versionCommand(EmptyParameters&) {
    std::cout << "nomadc " << getNomadVersion() << '\n';
    return EXIT_SUCCESS;
}

int checkCommand(CheckParameters& parameters) {
    const auto paths = resolveRequestedPaths(parameters.path);
    const HeadlessGame game(paths.resources);
    auto result = checkPaths(paths.sources, game.getRuntime());

    if (paths.entryFunction) {
        validateEntryFunction(result, game.getRuntime(), *paths.entryFunction, paths.projectFile);
    }

    printDiagnostics(result);
    return result.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
}

int dumpCommand(DumpParameters& parameters) {
    const auto paths = resolveRequestedPaths(parameters.path);
    const HeadlessGame game(paths.resources);
    const auto result = dumpInstructions(paths.sources, parameters.function, game.getRuntime());
    printDiagnostics(result.compilation);

    if (!result.compilation.succeeded()) {
        return EXIT_FAILURE;
    }

    std::cout << result.instructions;
    return EXIT_SUCCESS;
}

int documentationCommand(DocumentationParameters& parameters) {
    const auto outputPath = pathFromUtf8(parameters.output);
    const auto paths = resolveRequestedPaths(parameters.path);
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

} // namespace

int main(int argc, char** argv) {
    boost::nowide::args utf8Arguments(argc, argv);

    log::setLogLevel(LogLevel::Warning);
    log::clear();

    try {
        ProgramOptions program("nomadc", "Nomad compiler tooling");
        program.addGlobal("help", "h", [&program] {
            program.printHelp(std::cout);
            return EXIT_SUCCESS;
        }, "Show general help");
        program.addGlobal("version", "v", [] {
            EmptyParameters parameters;
            return versionCommand(parameters);
        }, "Show the Nomad version");
        program.addVerb<HelpParameters>("help", [&program](HelpParameters& parameters) {
            program.printHelp(std::cout, parameters.verb);
            return EXIT_SUCCESS;
        }, "Show general or command help")
            .addOptionalPositional("verb", &HelpParameters::verb, "Command to describe");
        program.addVerb<EmptyParameters>("version", &versionCommand, "Show the Nomad version");
        program.addVerb<CheckParameters>("check", &checkCommand, "Compile scripts without executing them")
            .addOptionalPositional("path", &CheckParameters::path, "Source path (default: discover the current project)");
        program.addVerb<DumpParameters>("dump", &dumpCommand, "Print generated VM instructions without execution")
            .addOptionalPositional("path", &DumpParameters::path, "Source path (default: discover the current project)")
            .addOption("function", "", &DumpParameters::function, "Function to dump (default: all functions)");
        program.addVerb<DocumentationParameters>("docs", &documentationCommand, "Generate language and engine API documentation")
            .addOptionalPositional("path", &DocumentationParameters::path, "Source path (default: discover the current project)")
            .addRequiredOption("output", "o", &DocumentationParameters::output, "Output Markdown file (required)");
        return program.run(argc, argv, std::cout);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
