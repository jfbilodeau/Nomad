// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectConfiguration.hpp>
#include <nomad/project/ProjectInitializer.hpp>
#include <nomad/project/ProjectPackager.hpp>
#include <nomad/system/Path.hpp>
#include <nomad/Version.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/nowide/args.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/start_dir.hpp>
#include <boost/program_options.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace nomad;

namespace {

namespace po = boost::program_options;
namespace bp = boost::process::v2;

void printUsage(std::ostream& output, const po::options_description& options) {
    output << options << '\n';
}

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

NomadPath getRequestedDirectory(const po::variables_map& arguments) {
    return arguments.contains("directory")
        ? pathFromUtf8(arguments["directory"].as<NomadString>())
        : std::filesystem::current_path();
}

ProjectConfiguration loadCompatibleProject(const po::variables_map& arguments) {
    const auto configuration = discoverProjectConfiguration(getRequestedDirectory(arguments));
    validateNomadVersionCompatibility(configuration.nomad.version, getNomadVersion());
    return configuration;
}

int versionCommand(const po::variables_map& arguments) {
    if (arguments.contains("directory")) {
        throw po::error("The version command does not accept arguments");
    }

    std::cout << "nomad " << getNomadVersion() << '\n';
    return EXIT_SUCCESS;
}

int initializeCommand(const po::variables_map& arguments) {
    const auto result = initializeProject(getRequestedDirectory(arguments), resolveProjectTemplate("default"));

    for (const auto& path : result.createdFiles) {
        std::cout << "Created " << pathToUtf8(path) << '\n';
    }

    return EXIT_SUCCESS;
}

int checkCommand(const po::variables_map& arguments) {
    const auto configuration = loadCompatibleProject(arguments);
    return runSiblingExecutable("nomadc", configuration.root, {"check"});
}

int runCommand(const po::variables_map& arguments) {
    const auto configuration = loadCompatibleProject(arguments);
    std::vector<NomadString> runtimeArguments;

    if (arguments.contains("debug")) {
        runtimeArguments.emplace_back("--debug");
    }

    return runSiblingExecutable("nomad-runtime", configuration.root, runtimeArguments);
}

int packageCommand(const po::variables_map& arguments) {
    const auto configuration = loadCompatibleProject(arguments);
    const auto checkResult = runSiblingExecutable("nomadc", configuration.root, {"check"});

    if (checkResult != EXIT_SUCCESS) {
        return checkResult;
    }

    const auto result = packageProject(
        configuration,
        resolveRuntimeBundle(),
        arguments.contains("force")
    );
    std::cout << "Packaged " << pathToUtf8(result.output) << '\n';
    return EXIT_SUCCESS;
}

int dispatchCommand(
    const NomadStringView command,
    const po::variables_map& arguments,
    const po::options_description& visibleOptions
) {
    if (arguments.contains("debug") && command != "run") {
        throw po::error("The --debug option is supported only by the run command");
    }

    if (arguments.contains("force") && command != "package") {
        throw po::error("The --force option is supported only by the package command");
    }

    if (command == "version") {
        return versionCommand(arguments);
    }

    if (command == "init") {
        return initializeCommand(arguments);
    }

    if (command == "check") {
        return checkCommand(arguments);
    }

    if (command == "run") {
        return runCommand(arguments);
    }

    if (command == "package") {
        return packageCommand(arguments);
    }

    std::cerr << "Unknown command: " << command << '\n';
    printUsage(std::cerr, visibleOptions);
    return EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv) {
    boost::nowide::args utf8Arguments(argc, argv);

    po::options_description visibleOptions(
        "Usage:\n"
        "  nomad init [directory]\n"
        "  nomad check [directory]\n"
        "  nomad run [directory] [--debug]\n"
        "  nomad package [directory] [--force]\n"
        "  nomad version\n"
        "\n"
        "Options"
    );
    visibleOptions.add_options()
        ("help,h", "Show this help")
        ("version,v", "Show the Nomad version")
        ("debug", "Enable runtime debug mode")
        ("force", "Replace a nonempty package output directory");

    po::options_description hiddenOptions;
    hiddenOptions.add_options()
        ("command", po::value<NomadString>(), "Command to run")
        ("directory", po::value<NomadString>(), "Project directory");

    po::options_description allOptions;
    allOptions.add(visibleOptions).add(hiddenOptions);

    po::positional_options_description positional;
    positional.add("command", 1);
    positional.add("directory", 1);

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
            if (
                arguments.contains("command") ||
                arguments.contains("directory") ||
                arguments.contains("debug") ||
                arguments.contains("force")
            ) {
                throw po::error("The version option does not accept arguments");
            }

            std::cout << "nomad " << getNomadVersion() << '\n';
            return EXIT_SUCCESS;
        }

        if (!arguments.contains("command")) {
            printUsage(std::cerr, visibleOptions);
            return EXIT_FAILURE;
        }

        const auto& command = arguments["command"].as<NomadString>();
        return dispatchCommand(command, arguments, visibleOptions);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
