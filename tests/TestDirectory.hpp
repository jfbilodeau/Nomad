// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace nomad::test {

class TestDirectory {
public:
    explicit TestDirectory(const std::string& name) :
        m_path(createTemporaryDirectory(name)) {
    }

    TestDirectory(const TestDirectory&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;

    ~TestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] const std::filesystem::path& getPath() const {
        return m_path;
    }

    [[nodiscard]] std::string file(const std::string& name) const {
        return (m_path / name).string();
    }

    std::filesystem::path write(
        const std::filesystem::path& relativePath,
        const std::string& content
    ) const {
        const auto path = m_path / relativePath;
        std::filesystem::create_directories(path.parent_path());

        std::ofstream file(path, std::ios::binary);

        if (!file.is_open()) {
            throw std::filesystem::filesystem_error(
                "Failed to create test file",
                path,
                std::make_error_code(std::errc::io_error)
            );
        }

        file << content;

        return path;
    }

private:
    static std::filesystem::path createTemporaryDirectory(const std::string& name) {
        const auto root = resolveTemporaryRoot();
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (auto attempt = 0; attempt < 100; ++attempt) {
            const auto path = root / (name + "-" + std::to_string(timestamp) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(path)) {
                return path;
            }
        }
        throw std::filesystem::filesystem_error(
            "Failed to create a unique test directory",
            root,
            std::make_error_code(std::errc::file_exists)
        );
    }

    static std::filesystem::path resolveTemporaryRoot() {
        std::error_code error;
        auto root = std::filesystem::temp_directory_path(error);

        if (!error) {
            return root;
        }

        error.clear();
        root = std::filesystem::current_path(error);

        if (error) {
            throw std::filesystem::filesystem_error("Failed to resolve a test directory", error);
        }

        return root;
    }

    std::filesystem::path m_path;
};

} // namespace nomad::test
