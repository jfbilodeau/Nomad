// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/CompilerContext.hpp>
#include <nomad/compiler/CompilerTools.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/resource/SpritePacker.hpp>

#include <nomad/script/Documentation.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/ProgramOptions.hpp>

#include <nomad/Version.hpp>

#include <boost/nowide/args.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
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

struct TextMateParameters {
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

int writeOutputFile(
    const NomadPath& outputPath,
    const NomadString& outputDescription,
    const NomadString& contents
) {
    std::ofstream outputStream(outputPath, std::ios::binary);

    if (!outputStream.is_open()) {
        std::cerr << "Failed to open " << outputDescription << " output: " << pathToUtf8(outputPath) << '\n';
        return EXIT_FAILURE;
    }

    outputStream << contents;

    if (!outputStream) {
        std::cerr << "Failed to write " << outputDescription << " output: " << pathToUtf8(outputPath) << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

NomadPath resolveDocumentationOutputPath(const NomadString& output) {
    auto outputPath = pathFromUtf8(output);

    if (std::filesystem::is_directory(outputPath)) {
        outputPath /= "nomad-reference.md";
    }

    return outputPath;
}

struct CompilationPaths {
    std::vector<NomadPath> sources;
    NomadPath resources;
    std::optional<NomadString> entryFunction;
    NomadPath projectFile;
    std::optional<SpriteOptions> sprites;
    std::optional<ScriptOptions> scripts;
    bool missingSpriteDirectoriesAllowed = false;
};

CompilationPaths resolveCompilationPaths(const NomadPath& requestedPath, const bool explicitPath) {
    auto resourcePath = std::filesystem::is_regular_file(requestedPath)
        ? requestedPath.parent_path()
        : requestedPath;
    std::vector<NomadPath> sourcePaths{requestedPath};

    if (std::filesystem::exists(requestedPath)) {
        const auto projectRoot = findProjectRoot(requestedPath);

        if (projectRoot) {
            const auto project = loadNomadProject(*projectRoot / NOMAD_PROJECT_FILE_NAME);
            resourcePath = resolveProjectResourcePath(project);

            if (!explicitPath) {
                const auto directories = project.gameOptions.scripts.directories.value_or(
                    std::vector<NomadPath>{"scripts"}
                );

                sourcePaths.clear();
                for (const auto& directory : directories) {
                    sourcePaths.push_back(resourcePath / directory);
                }

                return CompilationPaths{
                    std::move(sourcePaths),
                    resourcePath,
                    project.gameOptions.entryFunction,
                    *projectRoot / NOMAD_PROJECT_FILE_NAME,
                    project.gameOptions.sprites,
                    project.gameOptions.scripts,
                    project.configuration.type == ProjectConfigurationType::Release
                };
            }
        }
    }

    if (resourcePath.empty()) {
        resourcePath = std::filesystem::current_path();
    }

    return CompilationPaths{
        std::move(sourcePaths),
        resourcePath,
        std::nullopt,
        {},
        std::nullopt,
        std::nullopt,
        false
    };
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

bool checkSprites(const CompilationPaths& paths) {
    if (!paths.sprites) {
        return true;
    }

    if (paths.sprites->directories && paths.sprites->directories->empty()) {
        std::cout << "Sprites: disabled\n";
        return true;
    }

    try {
        const auto directories = paths.sprites->directories.value_or(
            std::vector<NomadPath>{"sprites"}
        );
        const auto report = analyzeSpritePacking(
            paths.resources,
            directories,
            paths.sprites->directories.has_value(),
            paths.sprites->maxTextureSize.value_or(2048),
            paths.sprites->padding.value_or(1),
            paths.missingSpriteDirectoriesAllowed
        );

        std::cout
            << "Sprites: " << report.sourceCount
            << " logical sprites, " << report.groupCount
            << " groups, " << report.pageCount
            << " texture pages\n";

        for (const auto& page : report.pages) {
            const auto totalPixelCount =
                page.width * page.height;
            const auto occupancy = totalPixelCount == 0
                ? 0.0
                : static_cast<double>(page.usedPixelCount) * 100.0 /
                    static_cast<double>(totalPixelCount);
            std::ostringstream line;
            line
                << "  " << page.name << ": "
                << page.width << 'x' << page.height << ", "
                << page.spriteCount << " sprites ("
                << page.uniqueSpriteCount << " unique), "
                << std::fixed << std::setprecision(1) << occupancy << "% occupancy\n";
            std::cout << line.str();
        }

        return true;
    } catch (const std::exception& error) {
        std::cerr
            << pathToGenericUtf8(paths.projectFile)
            << ": error: Failed to check sprites: "
            << error.what() << '\n';
        return false;
    }
}

int checkCommand(CheckParameters& parameters) {
    const auto paths = resolveRequestedPaths(parameters.path);
    const HeadlessGame game(paths.resources);
    auto result = checkPaths(paths.sources, game.getRuntime());

    if (paths.entryFunction) {
        validateEntryFunction(result, game.getRuntime(), *paths.entryFunction, paths.projectFile);
    }

    printDiagnostics(result);
    const auto spritesSucceeded = checkSprites(paths);
    return result.succeeded() && spritesSucceeded ? EXIT_SUCCESS : EXIT_FAILURE;
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
    const auto outputPath = resolveDocumentationOutputPath(parameters.output);

    if (!parameters.path) {
        const auto currentPath = std::filesystem::current_path();

        if (!findProjectRoot(currentPath)) {
            std::cerr
                << "warning: No Nomad project found from "
                << pathToUtf8(currentPath)
                << "; generating engine-only documentation.\n";

            const HeadlessGame game(currentPath);
            std::ostringstream output;
            generateDocumentation(game.getRuntime(), output);
            return writeOutputFile(outputPath, "documentation", output.str());
        }
    }

    const auto paths = resolveRequestedPaths(parameters.path);
    const HeadlessGame game(paths.resources);
    const auto result = generateDocumentationForPaths(paths.sources, game.getRuntime());
    printDiagnostics(result.compilation);

    if (!result.compilation.succeeded()) {
        return EXIT_FAILURE;
    }

    return writeOutputFile(outputPath, "documentation", result.documentation);
}

int textMateCommand(TextMateParameters& parameters) {
    const auto paths = resolveRequestedPaths(parameters.path);
    const HeadlessGame game(paths.resources);
    std::ostringstream output;
    generateTextMateGrammar(game.getRuntime(), output);
    return writeOutputFile(pathFromUtf8(parameters.output), "TextMate grammar", output.str());
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
            .addRequiredOption(
                "output",
                "o",
                &DocumentationParameters::output,
                "Output Markdown file or directory (required)"
            );
        program.addVerb<TextMateParameters>("textmate", &textMateCommand, "Generate a VS Code TextMate grammar")
            .addOptionalPositional("path", &TextMateParameters::path, "Project path (default: discover the current project)")
            .addRequiredOption("output", "o", &TextMateParameters::output, "Output .tmLanguage.json file (required)");
        return program.run(argc, argv, std::cout);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
