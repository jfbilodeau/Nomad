// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

namespace nomad {

DeclareId(LogSinkId);

enum class LogLevel {
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Critical = 5,
    Fatal = 6,
};

enum class LogDeliveryMode {
    Buffered = 1,
    Synchronous = 2,
};

struct SinkConfiguration {
    bool active = true;
    LogLevel level = LogLevel::Debug;
};

struct ConsoleSinkConfiguration : SinkConfiguration {};

struct MemorySinkConfiguration : SinkConfiguration {};

[[nodiscard]] constexpr bool isValidLogLevel(const LogLevel level) noexcept {
    return level >= LogLevel::Debug && level <= LogLevel::Fatal;
}

} // namespace nomad
