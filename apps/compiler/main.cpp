// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/compiler/Compilation.hpp>
#include <nomad/compiler/CompilerContext.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void printUsage(std::ostream& out) {
    out << "Usage: nomadc check [path]\n";
}

} // namespace

int main(const int argc, char** argv) {
    if (argc < 2) {
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }

    const std::string_view command(argv[1]);

    if (command == "--help" || command == "-h") {
        printUsage(std::cout);
        return EXIT_SUCCESS;
    }

    if (command != "check") {
        std::cerr << "Unknown command: " << command << '\n';
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }

    if (argc > 3) {
        std::cerr << "The check command accepts at most one path\n";
        printUsage(std::cerr);
        return EXIT_FAILURE;
    }

    const auto path = argc == 3 ? std::filesystem::path(argv[2]) : std::filesystem::current_path();
    const auto result = nomad::checkPath(path);

    for (const auto& diagnostic : result.diagnostics) {
        std::cerr << nomad::formatDiagnostic(diagnostic) << '\n';
    }

    return result.succeeded() ? EXIT_SUCCESS : EXIT_FAILURE;
}
