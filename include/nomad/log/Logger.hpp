// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <chrono>
#include <source_location>
#include <string>
#include <vector>

namespace nomad {

enum class LogLevel {
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Fatal = 5,
};

struct Location {
    explicit Location(const std::source_location& location = std::source_location::current()) {
        function = location.function_name();
        file = location.file_name();
        line = location.line();
        column = location.column();
    }

    Location(const NomadChar* function, const NomadChar* file, NomadInteger line, NomadInteger column):
        function(function),
        file(file),
        line(line),
        column(column)
    {}

    const NomadChar* function = "";
    const NomadChar* file = "";
    NomadInteger line = 0;
    NomadInteger column = 0;
};

struct LogEntry {
    std::chrono::system_clock::time_point time;
    LogLevel level;
    NomadString message;
};

class LogSink {
public:
    LogSink() = default;
    LogSink(const LogSink&) = delete;
    virtual ~LogSink() = default;

    // Allow sink to initialize itself before logs are flushed
    virtual void begin() { /* Override as necessary */ }

    virtual void log(const LogEntry* entry) = 0;

    // Signals the end of the log flush
    virtual void end() { /* Override as necessary */ }
};

class Logger {
public:
    explicit Logger(LogSink* sink);

    Logger(const Logger& other) = delete;

    ~Logger();

    void setLogLevel(LogLevel level);
    [[nodiscard]] LogLevel getLogLevel() const;

    void debug(NomadStringView message, const Location& location = Location());
    void info(NomadStringView message, const Location& location = Location());
    void warning(NomadStringView message, const Location& location = Location());
    void error(NomadStringView message, const Location& location = Location());
    void fatal(NomadStringView message, const Location& location = Location());

    void addSink(LogSink* sink);
    void removeSink(LogSink* sink);

    void clear();
    void flush();

private:
    void log(LogLevel level, NomadStringView message, const Location& location);

    LogLevel m_logLevel = LogLevel::Info;
    std::vector<LogSink*> m_sinks;
    std::vector<LogEntry> m_entries;
};

namespace log {

void setLogLevel(LogLevel level);
[[nodiscard]] LogLevel getLogLevel();

void debug(NomadStringView message, const Location& location = Location());
void info(NomadStringView message, const Location& location = Location());
void warning(NomadStringView message, const Location& location = Location());
void error(NomadStringView message, const Location& location = Location());
void fatal(NomadStringView message, const Location& location = Location());

void addSink(LogSink* sink);
void removeSink(LogSink* sink);

void clear();
void flush();

} // namespace log

} // namespace nomad
