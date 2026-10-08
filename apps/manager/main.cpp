// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectConfiguration.hpp>
#include <nomad/project/ProjectInitializer.hpp>
#include <nomad/Version.hpp>

#include <boost/program_options.hpp>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#if defined(_WIN32)
#include <process.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace nomad;

namespace {

namespace po = boost::program_options;

void printUsage(std::ostream& output, const po::options_description& options) {
    output
        << "Usage:\n"
        << "  nomad init [directory]\n"
        << "  nomad check [directory]\n"
        << "  nomad version\n"
        << '\n'
        << options
        << '\n';
}

NomadPath getExecutablePath([[maybe_unused]] const char* argumentZero) {
#if defined(_WIN32)
    std::wstring path(260U, L'\0');

    while (true) {
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));

        if (length == 0U) {
            throw NomadException("Failed to locate the Nomad executable");
        }

        if (static_cast<std::size_t>(length) < path.size()) {
            path.resize(length);
            return path;
        }

        path.resize(path.size() * 2U);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');

    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        throw NomadException("Failed to locate the Nomad executable");
    }

    return std::filesystem::canonical(path.c_str());
#elif defined(__linux__)
    return std::filesystem::canonical("/proc/self/exe");
#else
    return std::filesystem::absolute(argumentZero);
#endif
}

int runCompiler(const NomadPath& compilerPath, const NomadPath& projectRoot) {
    if (!std::filesystem::is_regular_file(compilerPath)) {
        throw NomadException(
            "Could not find the Nomad compiler beside the CLI at '" + compilerPath.string() + "'"
        );
    }

#if defined(_WIN32)
    const auto previousDirectory = std::filesystem::current_path();
    std::filesystem::current_path(projectRoot);
    const auto compilerName = compilerPath.filename();
    const wchar_t* arguments[] = {
        compilerName.c_str(),
        L"check",
        nullptr
    };
    const auto result = _wspawnv(_P_WAIT, compilerPath.c_str(), arguments);
    std::filesystem::current_path(previousDirectory);

    if (result == -1) {
        throw NomadException("Failed to run the Nomad compiler");
    }

    return static_cast<int>(result);
#else
    const auto processId = fork();

    if (processId == -1) {
        throw NomadException("Failed to create the Nomad compiler process");
    }

    if (processId == 0) {
        if (chdir(projectRoot.c_str()) != 0) {
            _exit(EXIT_FAILURE);
        }

        const auto compilerName = compilerPath.filename();
        execl(compilerPath.c_str(), compilerName.c_str(), "check", nullptr);
        _exit(127);
    }

    int status = 0;

    while (waitpid(processId, &status, 0) == -1) {
        if (errno != EINTR) {
            throw NomadException("Failed to wait for the Nomad compiler process");
        }
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return EXIT_FAILURE;
#endif
}

} // namespace

int main(const int argc, char** argv) {
    po::options_description visibleOptions("Options");
    visibleOptions.add_options()
        ("help,h", "Show this help")
        ("version,v", "Show the Nomad version");

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
            if (arguments.contains("command") || arguments.contains("directory")) {
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

        if (command == "version") {
            if (arguments.contains("directory")) {
                throw po::error("The version command does not accept arguments");
            }

            std::cout << "nomad " << getNomadVersion() << '\n';
            return EXIT_SUCCESS;
        }

        if (command == "init") {
            const auto destination = arguments.contains("directory")
                ? NomadPath(arguments["directory"].as<NomadString>())
                : std::filesystem::current_path();
            const auto result = initializeProject(destination);

            for (const auto& path : result.createdFiles) {
                std::cout << "Created " << path.string() << '\n';
            }

            return EXIT_SUCCESS;
        }

        if (command == "check") {
            const auto destination = arguments.contains("directory")
                ? NomadPath(arguments["directory"].as<NomadString>())
                : std::filesystem::current_path();
            const auto configuration = discoverProjectConfiguration(destination);
            validateNomadVersionCompatibility(configuration.nomad.version, getNomadVersion());

            auto compilerPath = getExecutablePath(argv[0]).parent_path() / "nomadc";
#if defined(_WIN32)
            compilerPath += ".exe";
#endif
            return runCompiler(compilerPath, configuration.root);
        }

        std::cerr << "Unknown command: " << command << '\n';
        printUsage(std::cerr, visibleOptions);
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
