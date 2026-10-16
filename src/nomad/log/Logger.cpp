// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/Logger.hpp>

#include <nomad/log/ConsoleSink.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <memory_resource>
#include <ctime>

namespace nomad {

Logger::Logger(LogSink* sink) {
    addSink(sink);

    info("Logger initialized.");
}

Logger::~Logger() {
    info("Logger shutting down.");

    flush();
}

void Logger::setLogLevel(const LogLevel level) {
    m_logLevel = level;
}

LogLevel Logger::getLogLevel() const {
    return m_logLevel.load();
}

void Logger::debug(const NomadStringView message, const Location& location) {
    log(LogLevel::Debug, message, location);
}


void Logger::info(const NomadStringView message, const Location& location) {
    log(LogLevel::Info, message, location);
}

void Logger::warning(const NomadStringView message, const Location& location) {
    log(LogLevel::Warning, message, location);
}

void Logger::error(const NomadStringView message, const Location& location) {
    log(LogLevel::Error, message, location);

    // Make sure the error is logged immediately.
    flush();
}

void Logger::fatal(const NomadStringView message, const Location& location) {
    log(LogLevel::Fatal, message, location);

    // Make sure the error is logged immediately.
    flush();

    throw NomadException(NomadString(message));
}

void Logger::addSink(LogSink* sink) {
    const auto lock = std::lock_guard(m_deliveryMutex);
    if (sink == nullptr) {
        throw NomadBug("Cannot register a null log sink");
    }
    m_sinks.push_back(sink);
}

void Logger::removeSink(LogSink* sink) {
    const auto lock = std::lock_guard(m_deliveryMutex);
    m_sinks.erase(std::ranges::remove(m_sinks, sink).begin(), m_sinks.end());
}

void Logger::clear() {
    const auto lock = std::lock_guard(m_entriesMutex);
    m_entries.clear();
}

void Logger::flush() {
    const auto deliveryLock = std::lock_guard(m_deliveryMutex);
    std::vector<LogEntry> entries;
    {
        const auto entriesLock = std::lock_guard(m_entriesMutex);
        entries.swap(m_entries);
    }
    const auto sinks = m_sinks;
    for (auto* sink: sinks) {
        if (std::ranges::find(m_sinks, sink) == m_sinks.end()) {
            continue;
        }
        sink->begin();

        for (const auto& entry: entries) {
            sink->log(&entry);
        }

        sink->end();
    }

}

void Logger::log(const LogLevel level, const NomadStringView message, const Location& location) {
    if (level < m_logLevel.load()) {
        // Skip!
        return;
    }

    NomadString logLevelName;

    switch (level) {
        case LogLevel::Debug:
            logLevelName = "DEBUG";
            break;
        case LogLevel::Info:
            logLevelName = "INFO";
            break;
        case LogLevel::Warning:
            logLevelName = "WARNING";
            break;
        case LogLevel::Error:
            logLevelName = "ERROR";
            break;
        case LogLevel::Fatal:
            logLevelName = "FATAL";
            break;
        default:
            logLevelName = "UNKNOWN";
            break;
    }

    auto time = std::chrono::system_clock::now();

    auto line = std::format(
        "{0:%F}T{0:%T} [{1}] [{2}] [{3}:{4}:{5}] {6}",
        time,
        logLevelName,
        location.function,
        location.file,
        location.line,
        location.column,
        message
    );

    const auto lock = std::lock_guard(m_entriesMutex);
    m_entries.emplace_back(LogEntry{
        time,
        level,
        line
    });
}

namespace log {

namespace {

Logger& getGlobalLogger() {
    static ConsoleSink consoleSink;
    static Logger logger(&consoleSink);
    return logger;
}

} // namespace

void setLogLevel(const LogLevel level) {
    getGlobalLogger().setLogLevel(level);
}

LogLevel getLogLevel() {
    return getGlobalLogger().getLogLevel();
}

void debug(const NomadStringView message, const Location& location) {
    getGlobalLogger().debug(message, location);
}

void info(const NomadStringView message, const Location& location) {
    getGlobalLogger().info(message, location);
}

void warn(const NomadStringView message, const Location& location) {
    getGlobalLogger().warning(message, location);
}

void warning(const NomadStringView message, const Location& location) {
    getGlobalLogger().warning(message, location);
}

void error(const NomadStringView message, const Location& location) {
    getGlobalLogger().error(message, location);
}

void fatal(const NomadStringView message, const Location& location) {
    getGlobalLogger().fatal(message, location);
}

void addSink(LogSink* sink) {
    getGlobalLogger().addSink(sink);
}

void removeSink(LogSink* sink) {
    getGlobalLogger().removeSink(sink);
}

void clear() {
    getGlobalLogger().clear();
}

void flush() {
    getGlobalLogger().flush();
}

} // log

} // nomad
