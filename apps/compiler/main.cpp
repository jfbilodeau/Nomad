// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/SourceLoader.hpp>

#include <nomad/log/Logger.hpp>

#include <nomad/project/ProjectConfiguration.hpp>

#include <nomad/script/Documentation.hpp>
#include <nomad/script/InstructionDump.hpp>
#include <nomad/script/Runtime.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/ProgramOptions.hpp>
#include <nomad/system/ProjectSession.hpp>

#include <nomad/Version.hpp>

#include <boost/nowide/args.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
        formatDiagnostic(diagnostic, std::cerr);
        std::cerr << '\n';
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
    NomadPath projectFile;
    std::optional<NomadProject> project;
};

CompilationPaths resolveCompilationPaths(const NomadPath& requestedPath, const bool explicitPath) {
    auto resourcePath = std::filesystem::is_regular_file(requestedPath)
        ? requestedPath.parent_path()
        : requestedPath;
    std::vector<NomadPath> sourcePaths{requestedPath};
    std::optional<NomadProject> project;

    if (std::filesystem::exists(requestedPath)) {
        const auto projectRoot = findProjectRoot(requestedPath);

        if (projectRoot) {
            project = loadNomadProject(*projectRoot / NOMAD_PROJECT_FILE_NAME);
            resourcePath = project->root;

            if (!explicitPath) {
                sourcePaths = resolveScriptPaths(project->document, project->root);

                return CompilationPaths{
                    std::move(sourcePaths),
                    resourcePath,
                    *projectRoot / NOMAD_PROJECT_FILE_NAME,
                    std::move(project)
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
        {},
        std::move(project)
    };
}

std::unique_ptr<ModuleHost> createCompilerSession(
    const CompilationPaths& paths,
    const std::vector<NomadString>& arguments
) {
    if (paths.project) {
        return createProjectSession(*paths.project, ModuleMode::Analysis, arguments);
    }
    NomadProject project;
    project.root = paths.resources;
    project.document.setString("resources.directory", ".");
    project.configuration.sdk.modules = {"game"};
    return ModuleHost::create(
        project.document, project.root, resolveProjectModules(project, getSdkDirectory()),
        arguments, ModuleMode::Analysis
    );
}

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

bool checkResources(const CompilationPaths& paths, ModuleHost& session) {
    if (paths.projectFile.empty()) {
        return true;
    }

    try {
        std::cout << session.checkResources();
        return true;
    } catch (const std::exception& error) {
        std::cerr
            << pathToGenericUtf8(paths.projectFile)
            << ": error: Failed to check sprites: "
            << error.what() << '\n';
        return false;
    }
}

int checkCommand(CheckParameters& parameters, const std::vector<NomadString>& arguments) {
    const auto paths = resolveRequestedPaths(parameters.path);
    const auto session = createCompilerSession(paths, arguments);
    const auto result = session->compileSources(loadSourcePaths(paths.sources));

    printDiagnostics(result);
    const auto resourcesSucceeded = checkResources(paths, *session);
    return result.succeeded() && resourcesSucceeded ? EXIT_SUCCESS : EXIT_FAILURE;
}

int dumpCommand(DumpParameters& parameters, const std::vector<NomadString>& arguments) {
    const auto paths = resolveRequestedPaths(parameters.path);
    const auto session = createCompilerSession(paths, arguments);
    const auto result = session->compileSources(loadSourcePaths(paths.sources));
    printDiagnostics(result);

    if (!result.succeeded()) {
        return EXIT_FAILURE;
    }

    const auto& runtime = *session->getRuntime();
    std::ostringstream output;
    if (parameters.function) {
        const auto* function = runtime.getScriptFunction(runtime.getFunctionId(*parameters.function));
        if (function == nullptr) {
            formatDiagnostic({
                DiagnosticSeverity::Error, "Unknown function '" + *parameters.function + "'",
                paths.sources.empty() ? NomadString{} : pathToGenericUtf8(paths.sources.front()),
                NOMAD_INVALID_INDEX, NOMAD_INVALID_INDEX
            }, std::cerr);
            std::cerr << '\n';
            return EXIT_FAILURE;
        }
        dumpInstructions(runtime, *function, output);
    } else {
        dumpInstructions(runtime, output);
    }
    std::cout << output.str();
    return EXIT_SUCCESS;
}

int documentationCommand(DocumentationParameters& parameters, const std::vector<NomadString>& arguments) {
    const auto outputPath = resolveDocumentationOutputPath(parameters.output);

    if (!parameters.path) {
        const auto currentPath = std::filesystem::current_path();

        if (!findProjectRoot(currentPath)) {
            std::cerr
                << "warning: No Nomad project found from "
                << pathToUtf8(currentPath)
                << "; generating engine-only documentation.\n";

            const auto session = createCompilerSession(resolveCompilationPaths(currentPath, true), arguments);
            std::ostringstream output;
            generateDocumentation(session->getRuntime(), output);
            return writeOutputFile(outputPath, "documentation", output.str());
        }
    }

    const auto paths = resolveRequestedPaths(parameters.path);
    const auto session = createCompilerSession(paths, arguments);
    const auto result = session->compileSources(loadSourcePaths(paths.sources));
    printDiagnostics(result);

    if (!result.succeeded()) {
        return EXIT_FAILURE;
    }

    std::ostringstream output;
    generateDocumentation(session->getRuntime(), output);
    return writeOutputFile(outputPath, "documentation", output.str());
}

int textMateCommand(TextMateParameters& parameters, const std::vector<NomadString>& arguments) {
    const auto paths = resolveRequestedPaths(parameters.path);
    const auto session = createCompilerSession(paths, arguments);
    std::ostringstream output;
    generateTextMateGrammar(session->getRuntime(), output);
    return writeOutputFile(pathFromUtf8(parameters.output), "TextMate grammar", output.str());
}

} // namespace

int main(int argc, char** argv) {
    boost::nowide::args utf8Arguments(argc, argv);
    const std::vector<NomadString> arguments(argv, argv + argc);

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
        program.addVerb<CheckParameters>("check", [&arguments](CheckParameters& parameters) {
            return checkCommand(parameters, arguments);
        }, "Compile scripts without executing them")
            .addOptionalPositional("path", &CheckParameters::path, "Source path (default: discover the current project)");
        program.addVerb<DumpParameters>("dump", [&arguments](DumpParameters& parameters) {
            return dumpCommand(parameters, arguments);
        }, "Print generated VM instructions without execution")
            .addOptionalPositional("path", &DumpParameters::path, "Source path (default: discover the current project)")
            .addOption("function", "", &DumpParameters::function, "Function to dump (default: all functions)");
        program.addVerb<DocumentationParameters>("docs", [&arguments](DocumentationParameters& parameters) {
            return documentationCommand(parameters, arguments);
        }, "Generate language and engine API documentation")
            .addOptionalPositional("path", &DocumentationParameters::path, "Source path (default: discover the current project)")
            .addRequiredOption(
                "output",
                "o",
                &DocumentationParameters::output,
                "Output Markdown file or directory (required)"
            );
        program.addVerb<TextMateParameters>("textmate", [&arguments](TextMateParameters& parameters) {
            return textMateCommand(parameters, arguments);
        }, "Generate a VS Code TextMate grammar")
            .addOptionalPositional("path", &TextMateParameters::path, "Project path (default: discover the current project)")
            .addRequiredOption("output", "o", &TextMateParameters::output, "Output .tmLanguage.json file (required)");
        return program.run(argc, argv, std::cout);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
