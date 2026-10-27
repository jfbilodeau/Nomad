// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/log/SinkConfiguration.hpp>

#include <atomic>
#include <chrono>

namespace nomad {

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

    [[nodiscard]] bool getActive() const;
    void setActive(bool active);
    [[nodiscard]] LogLevel getLogLevel() const;
    void setLogLevel(LogLevel level);

    virtual void begin() {}
    virtual void log(const LogEntry* entry) = 0;
    virtual void end() {}

private:
    std::atomic<bool> m_active;
    std::atomic<LogLevel> m_logLevel;
};

} // namespace nomad
