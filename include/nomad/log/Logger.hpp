// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/log/LogSink.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <source_location>
#include <string>
#include <vector>

namespace nomad {

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

class Logger {
public:
    Logger();

    Logger(const Logger& other) = delete;

    ~Logger();

    void setLogLevel(LogLevel level);
    [[nodiscard]] LogLevel getLogLevel() const;
    void setDeliveryMode(LogDeliveryMode mode);
    [[nodiscard]] LogDeliveryMode getDeliveryMode() const;

    void debug(NomadStringView message, const Location& location = Location());
    void info(NomadStringView message, const Location& location = Location());
    void warning(NomadStringView message, const Location& location = Location());
    void error(NomadStringView message, const Location& location = Location());
    void critical(NomadStringView message, const Location& location = Location());
    [[noreturn]] void fatal(NomadStringView message, const Location& location = Location());

    [[nodiscard]] LogSinkId createConsoleSink(const ConsoleSinkConfiguration& configuration = {});
    [[nodiscard]] LogSinkId createMemorySink(const MemorySinkConfiguration& configuration = {});
    [[nodiscard]] LogSinkId createSink(std::unique_ptr<LogSink> sink);
    [[nodiscard]] LogSink* getSink(LogSinkId sinkId);
    [[nodiscard]] const LogSink* getSink(LogSinkId sinkId) const;
    void removeSink(LogSinkId sinkId);
    void clearAllSinks();
    void replaceSinks(const std::vector<LogSinkId>& sinkIds);

    void clear();
    void flush();

private:
    struct SinkRegistration {
        LogSinkId id;
        std::unique_ptr<LogSink> sink;
    };

    void log(LogLevel level, NomadStringView message, const Location& location);
    void requireIdleDelivery() const;

    std::atomic<LogLevel> m_logLevel = LogLevel::Info;
    mutable std::recursive_mutex m_mutex;
    LogDeliveryMode m_deliveryMode = LogDeliveryMode::Buffered;
    bool m_delivering = false;
    NomadId m_nextSinkId = 0;
    std::vector<SinkRegistration> m_sinks;
    std::vector<LogEntry> m_entries;
};

namespace log {

[[nodiscard]] Logger& getLogger();
void setLogLevel(LogLevel level);
[[nodiscard]] LogLevel getLogLevel();
void setDeliveryMode(LogDeliveryMode mode);
[[nodiscard]] LogDeliveryMode getDeliveryMode();

void debug(NomadStringView message, const Location& location = Location());
void info(NomadStringView message, const Location& location = Location());
// void warn(NomadStringView message, const Location& location = Location());
void warning(NomadStringView message, const Location& location = Location());
void error(NomadStringView message, const Location& location = Location());
void critical(NomadStringView message, const Location& location = Location());
[[noreturn]] void fatal(NomadStringView message, const Location& location = Location());

[[nodiscard]] LogSinkId createConsoleSink(const ConsoleSinkConfiguration& configuration = {});
[[nodiscard]] LogSinkId createMemorySink(const MemorySinkConfiguration& configuration = {});
[[nodiscard]] LogSinkId createSink(std::unique_ptr<LogSink> sink);
[[nodiscard]] LogSink* getSink(LogSinkId sinkId);
void removeSink(LogSinkId sinkId);
void clearAllSinks();
void replaceSinks(const std::vector<LogSinkId>& sinkIds);

void clear();
void flush();

} // namespace log

} // namespace nomad
