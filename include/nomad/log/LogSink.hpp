// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <atomic>
#include <chrono>

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

struct SinkConfiguration {
    NomadBoolean active = true;
    LogLevel level = LogLevel::Info;
};

[[nodiscard]] constexpr bool isValidLogLevel(const LogLevel level) noexcept {
    return level >= LogLevel::Debug && level <= LogLevel::Fatal;
}

struct LogEntry {
    std::chrono::system_clock::time_point time;
    LogLevel level;
    NomadString message;
};

class LogSink {
public:
    explicit LogSink(const SinkConfiguration& configuration = {});
    LogSink(const LogSink&) = delete;
    virtual ~LogSink() = default;

    [[nodiscard]] NomadBoolean getActive() const;
    void setActive(NomadBoolean active);
    [[nodiscard]] LogLevel getLogLevel() const;
    void setLogLevel(LogLevel level);

    virtual void begin() {}
    virtual void log(const LogEntry* entry) = 0;
    virtual void end() {}

private:
    std::atomic<NomadBoolean> m_active;
    std::atomic<LogLevel> m_logLevel;
};

} // namespace nomad
