// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/project/ProjectPackager.hpp>

#include <nomad/system/Path.hpp>

#include <boost/nowide/args.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace nomad;

int main(int argc, char** argv) {
    boost::nowide::args utf8Arguments(argc, argv);

    if (argc != 3) {
        std::cerr << "Usage: nomad-packager <output.zip> <root>\n";
        return EXIT_FAILURE;
    }

    try {
        const auto output = std::filesystem::absolute(pathFromUtf8(argv[1])).lexically_normal();
        const auto root = std::filesystem::absolute(pathFromUtf8(argv[2])).lexically_normal();

        if (!std::filesystem::is_directory(root)) {
            throw ProjectPackagingError(
                "Archive root is not a directory: '" + pathToUtf8(root) + "'"
            );
        }

        std::vector<NomadPath> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_symlink()) {
                throw ProjectPackagingError(
                    "Archive input must not contain symbolic links: '" +
                    pathToUtf8(entry.path()) + "'"
                );
            }
            if (entry.is_regular_file()) {
                files.push_back(entry.path());
            } else if (!entry.is_directory()) {
                throw ProjectPackagingError(
                    "Unsupported archive input: '" + pathToUtf8(entry.path()) + "'"
                );
            }
        }

        std::ranges::sort(files, {}, [&root](const NomadPath& file) {
            return file.lexically_relative(root).generic_string();
        });
        writeDeterministicZipArchive(output, root, files);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "nomad-packager: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
