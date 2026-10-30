// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/log/LogSink.hpp>

#include <filesystem>
#include <fstream>

namespace nomad {

enum class FileRotationMode : NomadInteger {
    None = 1,
    Size,
};

struct FileSinkConfiguration : SinkConfiguration {
    NomadPath directory;
    NomadString name = "application";
    FileRotationMode rotationMode = FileRotationMode::Size;
    NomadBoolean rotateAtStartup = true;
    NomadInteger maxSizeBytes = 10 * 1024 * 1024;
    NomadInteger maxFiles = 10;
};

class FileSink : public LogSink {
public:
    explicit FileSink(const FileSinkConfiguration& configuration);
    FileSink(const FileSink&) = delete;
    ~FileSink() override = default;

    [[nodiscard]] const NomadPath& getPath() const;
    [[nodiscard]] FileRotationMode getRotationMode() const;
    void setRotationMode(FileRotationMode mode);
    [[nodiscard]] bool getRotateAtStartup() const;
    void setRotateAtStartup(bool rotateAtStartup);
    [[nodiscard]] NomadInteger getMaxSizeBytes() const;
    [[nodiscard]] NomadInteger getMaxFiles() const;
    void setRotationLimits(NomadInteger maxSizeBytes, NomadInteger maxFiles);

    void begin() override;
    void log(const LogEntry* entry) override;
    void end() override;

private:
    void open();
    void rotate();
    [[nodiscard]] std::filesystem::path getArchivePath(NomadIndex index) const;

    NomadPath m_path;
    NomadString m_name;
    FileRotationMode m_rotationMode;
    bool m_rotateAtStartup;
    NomadIndex m_maxSizeBytes = 0;
    NomadIndex m_maxFiles = 0;
    NomadIndex m_sizeBytes = 0;
    bool m_started = false;
    std::ofstream m_output;
};

} // namespace nomad
