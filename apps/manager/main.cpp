// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectInitializer.hpp>
#include <nomad/Version.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>

using namespace nomad;

namespace {

void printUsage(std::ostream& output) {
    output
        << "Usage:\n"
        << "  nomad init [directory]\n"
        << "  nomad version\n";
}

} // namespace

int main(const int argc, char** argv) {
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

        std::cout << "nomad " << NOMAD_VERSION << '\n';
        return EXIT_SUCCESS;
    }

    if (command == "init") {
        if (argc > 3) {
            std::cerr << "The init command accepts at most one directory\n";
            return EXIT_FAILURE;
        }

        try {
            const auto destination = argc == 3
                ? NomadPath(argv[2])
                : std::filesystem::current_path();
            const auto result = initializeProject(destination);

            for (const auto& path : result.createdFiles) {
                std::cout << "Created " << path.string() << '\n';
            }

            return EXIT_SUCCESS;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return EXIT_FAILURE;
        }
    }

    std::cerr << "Unknown command: " << command << '\n';
    printUsage(std::cerr);
    return EXIT_FAILURE;
}
