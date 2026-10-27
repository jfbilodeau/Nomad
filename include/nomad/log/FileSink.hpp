// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/log/LogSink.hpp>

#include <fstream>

namespace nomad {

class FileSink : public LogSink {
public:
    explicit FileSink(const FileSinkConfiguration& configuration);
    FileSink(const FileSink&) = delete;
    ~FileSink() override = default;

    [[nodiscard]] const std::filesystem::path& getPath() const;
    [[nodiscard]] FileRotationMode getRotationMode() const;
    void setRotationMode(FileRotationMode mode);
    [[nodiscard]] std::uintmax_t getMaxSizeBytes() const;
    [[nodiscard]] std::uintmax_t getMaxFiles() const;
    void setRotationLimits(std::uintmax_t maxSizeBytes, std::uintmax_t maxFiles);

    void begin() override;
    void log(const LogEntry* entry) override;
    void end() override;

private:
    void open();
    void rotate();
    [[nodiscard]] std::filesystem::path getArchivePath(std::uintmax_t index) const;

    std::filesystem::path m_path;
    NomadString m_name;
    FileRotationMode m_rotationMode;
    std::uintmax_t m_maxSizeBytes;
    std::uintmax_t m_maxFiles;
    std::uintmax_t m_sizeBytes = 0;
    bool m_started = false;
    std::ofstream m_output;
};

} // namespace nomad
