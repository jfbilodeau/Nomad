// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectConfiguration.hpp>
#include <nomad/project/ProjectInitializer.hpp>
#include <nomad/project/ProjectPackager.hpp>

#include <nomad/system/Path.hpp>
#include <nomad/system/ProgramOptions.hpp>

#include <nomad/Version.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/nowide/args.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/start_dir.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace nomad;

namespace {

namespace bp = boost::process::v2;

struct ProjectParameters {
    std::optional<NomadString> directory;
};

struct RunParameters {
    std::optional<NomadString> directory;
    bool debug = false;
};

struct PackageParameters {
    std::optional<NomadString> directory;
    bool force = false;
    bool dryRun = false;
};

struct HelpParameters {
    std::optional<NomadString> verb;
};

NomadPath getCliDirectory() {
    return NomadPath(boost::dll::program_location().native()).parent_path();
}

// Tools ship beside the CLI, so the executable suffix is taken from the running
// CLI instead of being selected with platform-specific conditionals. Paths are
// kept in their native encoding so non-ASCII directories survive the round trip.
NomadPath resolveSiblingExecutable(const NomadStringView executableName) {
    const auto directory = getCliDirectory();
    const auto cliPath = directory / NomadPath(boost::dll::program_location().native()).filename();
    auto suffixedCandidate = directory / pathFromUtf8(executableName);
    suffixedCandidate += cliPath.extension();
    const NomadPath candidates[] = {
        suffixedCandidate,
        directory / pathFromUtf8(executableName)
    };

    for (const auto& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }

    throw NomadException(
        "Could not find the Nomad " + NomadString(executableName) +
        " beside the CLI in '" + pathToUtf8(directory) + "'"
    );
}

NomadPath resolveProjectTemplate(const NomadStringView templateName) {
    const auto templateDirectory =
        getCliDirectory() / "templates" / "projects" / pathFromUtf8(templateName);

    if (!std::filesystem::is_directory(templateDirectory)) {
        throw NomadException(
            "Could not find the Nomad project template '" + NomadString(templateName) +
            "' beside the CLI in '" + pathToUtf8(templateDirectory) + "'"
        );
    }

    return templateDirectory;
}

NomadPath resolveRuntimeBundle() {
    const auto runtimeDirectory = getCliDirectory() / "runtime";

    if (!std::filesystem::is_directory(runtimeDirectory)) {
        throw NomadException(
            "Could not find the Nomad runtime bundle beside the CLI in '" +
            pathToUtf8(runtimeDirectory) + "'"
        );
    }

    return runtimeDirectory;
}

int runSiblingExecutable(
    const NomadStringView executableName,
    const NomadPath& projectRoot,
    const std::vector<NomadString>& arguments
) {
    const auto executablePath = resolveSiblingExecutable(executableName);
    boost::asio::io_context context;
    bp::process child(
        context,
        bp::filesystem::path(executablePath.native()),
        arguments,
        bp::process_start_dir(bp::filesystem::path(projectRoot.native()))
    );

    return child.wait();
}

NomadPath getRequestedDirectory(const std::optional<NomadString>& directory) {
    return directory
        ? pathFromUtf8(*directory)
        : std::filesystem::current_path();
}

ProjectConfiguration loadCompatibleProject(const std::optional<NomadString>& directory) {
    const auto configuration = discoverProjectConfiguration(getRequestedDirectory(directory));
    validateNomadVersionCompatibility(configuration.nomad.version, getNomadVersion());
    return configuration;
}

int versionCommand(EmptyParameters&) {
    std::cout << "nomad " << getNomadVersion() << '\n';
    return EXIT_SUCCESS;
}

int initializeCommand(ProjectParameters& parameters) {
    const auto result = initializeProject(getRequestedDirectory(parameters.directory), resolveProjectTemplate("default"));

    for (const auto& path : result.createdFiles) {
        std::cout << "Created " << pathToUtf8(path) << '\n';
    }

    return EXIT_SUCCESS;
}

int checkCommand(ProjectParameters& parameters) {
    const auto configuration = loadCompatibleProject(parameters.directory);
    return runSiblingExecutable("nomadc", configuration.root, {"check"});
}

int runCommand(RunParameters& parameters) {
    const auto configuration = loadCompatibleProject(parameters.directory);
    std::vector<NomadString> runtimeArguments;

    if (parameters.debug) {
        runtimeArguments.emplace_back("--debug");
    }

    return runSiblingExecutable("nomad-runtime", configuration.root, runtimeArguments);
}

int packageCommand(PackageParameters& parameters) {
    const auto configuration = loadCompatibleProject(parameters.directory);
    const auto checkResult = runSiblingExecutable("nomadc", configuration.root, {"check"});

    if (checkResult != EXIT_SUCCESS) {
        return checkResult;
    }

    const auto result = packageProject(
        configuration,
        resolveRuntimeBundle(),
        parameters.force,
        parameters.dryRun
    );

    if (parameters.dryRun) {
        std::cout << "Would package to " << pathToUtf8(result.output) << ":\n";

        for (const auto& file : result.files) {
            std::cout << "  " << pathToUtf8(file) << '\n';
        }
    } else {
        std::cout << "Packaged " << pathToUtf8(result.output) << '\n';
    }

    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    boost::nowide::args utf8Arguments(argc, argv);

    try {
        ProgramOptions program("nomad", "Nomad project manager");

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
        program.addVerb<ProjectParameters>("init", &initializeCommand, "Create a minimal Nomad project")
            .addOptionalPositional("directory", &ProjectParameters::directory, "Project directory (default: current directory)");
        program.addVerb<ProjectParameters>("check", &checkCommand, "Check project scripts without running them")
            .addOptionalPositional("directory", &ProjectParameters::directory, "Project directory (default: current directory)");
        program.addVerb<RunParameters>("run", &runCommand, "Launch a project using the installed runtime")
            .addOptionalPositional("directory", &RunParameters::directory, "Project directory (default: current directory)")
            .addFlag("debug", "", &RunParameters::debug, "Enable runtime debug mode");
        program.addVerb<PackageParameters>("package", &packageCommand, "Create a standalone game package")
            .addOptionalPositional("directory", &PackageParameters::directory, "Project directory (default: current directory)")
            .addFlag("force", "f", &PackageParameters::force, "Replace a nonempty package output directory")
            .addFlag("dry-run", "", &PackageParameters::dryRun, "List package files without writing them");
            
        return program.run(argc, argv, std::cout);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
