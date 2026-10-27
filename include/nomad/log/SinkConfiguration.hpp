// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <cstdint>
#include <filesystem>

namespace nomad {

DeclareId(LogSinkId);

enum class LogLevel {
    Debug = 1,
    Info,
    Warning,
    Error,
    Critical,
    Fatal,
};

enum class LogDeliveryMode {
    Buffered = 1,
    Synchronous = 2,
};

enum class FileRotationMode {
    None = 1,
    Startup,
    Size,
};

struct SinkConfiguration {
    bool active = true;
    LogLevel level = LogLevel::Info;
};

struct ConsoleSinkConfiguration : SinkConfiguration {};

struct FileSinkConfiguration : SinkConfiguration {
    std::filesystem::path directory;
    NomadString name = "application";
    FileRotationMode rotationMode = FileRotationMode::Size;
    std::uintmax_t maxSizeBytes = 10 * 1024 * 1024;
    std::uintmax_t maxFiles = 10;
};

struct MemorySinkConfiguration : SinkConfiguration {};

[[nodiscard]] constexpr bool isValidLogLevel(const LogLevel level) noexcept {
    return level >= LogLevel::Debug && level <= LogLevel::Fatal;
}

} // namespace nomad
