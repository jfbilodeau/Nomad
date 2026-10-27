// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/Logger.hpp>

#include <nomad/log/ConsoleSink.hpp>
#include <nomad/log/MemorySink.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <format>
#include <utility>

namespace nomad {

Logger::Logger() {
    (void)createConsoleSink();
    info("Logger initialized.");
}

Logger::~Logger() {
    try {
        info("Logger shutting down.");
        flush();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Logger shutdown failed: %s\n", error.what());
    }
}

void Logger::setLogLevel(const LogLevel level) {
    if (!isValidLogLevel(level)) {
        throw NomadException("Invalid logger level");
    }
    const auto lock = std::lock_guard(m_mutex);
    m_logLevel.store(level);
}

LogLevel Logger::getLogLevel() const {
    return m_logLevel.load();
}

void Logger::setDeliveryMode(const LogDeliveryMode mode) {
    if (mode != LogDeliveryMode::Buffered && mode != LogDeliveryMode::Synchronous) {
        throw NomadException("Invalid log delivery mode");
    }
    const auto lock = std::lock_guard(m_mutex);
    requireIdleDelivery();
    if (mode == LogDeliveryMode::Synchronous && m_deliveryMode != mode) {
        flush();
    }
    m_deliveryMode = mode;
}

LogDeliveryMode Logger::getDeliveryMode() const {
    const auto lock = std::lock_guard(m_mutex);
    return m_deliveryMode;
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
}

void Logger::fatal(const NomadStringView message, const Location& location) {
    log(LogLevel::Fatal, message, location);

    throw NomadException(NomadString(message));
}

void Logger::critical(const NomadStringView message, const Location& location) {
    log(LogLevel::Critical, message, location);
}

LogSinkId Logger::createConsoleSink(const ConsoleSinkConfiguration& configuration) {
    return createSink(std::make_unique<ConsoleSink>(configuration));
}

LogSinkId Logger::createMemorySink(const MemorySinkConfiguration& configuration) {
    return createSink(std::make_unique<MemorySink>(configuration));
}

LogSinkId Logger::createSink(std::unique_ptr<LogSink> sink) {
    if (!sink) {
        throw NomadException("Cannot create a null log sink");
    }
    const auto lock = std::lock_guard(m_mutex);
    if (!isValidId(m_nextSinkId)) {
        throw NomadException("Log sink ID limit reached");
    }
    const LogSinkId id{m_nextSinkId};
    m_sinks.push_back({id, std::move(sink)});
    ++m_nextSinkId;
    return id;
}

LogSink* Logger::getSink(const LogSinkId sinkId) {
    const auto lock = std::lock_guard(m_mutex);
    const auto found = std::ranges::find(m_sinks, sinkId, &SinkRegistration::id);
    return found == m_sinks.end() ? nullptr : found->sink.get();
}

const LogSink* Logger::getSink(const LogSinkId sinkId) const {
    const auto lock = std::lock_guard(m_mutex);
    const auto found = std::ranges::find(m_sinks, sinkId, &SinkRegistration::id);
    return found == m_sinks.end() ? nullptr : found->sink.get();
}

void Logger::removeSink(const LogSinkId sinkId) {
    const auto lock = std::lock_guard(m_mutex);
    requireIdleDelivery();
    const auto found = std::ranges::find(m_sinks, sinkId, &SinkRegistration::id);
    if (found == m_sinks.end()) {
        throw NomadException("Unknown log sink ID: " + std::to_string(sinkId.id));
    }
    const auto index = static_cast<NomadIndex>(found - m_sinks.begin());
    flush();
    m_sinks.erase(m_sinks.begin() + static_cast<std::ptrdiff_t>(index));
}

void Logger::clearAllSinks() {
    replaceSinks({});
}

void Logger::replaceSinks(const std::vector<LogSinkId>& sinkIds) {
    const auto lock = std::lock_guard(m_mutex);
    requireIdleDelivery();
    std::vector<NomadIndex> indices;
    std::vector<SinkRegistration> replacement;
    indices.reserve(sinkIds.size());
    replacement.reserve(sinkIds.size());
    for (const auto sinkId : sinkIds) {
        const auto found = std::ranges::find(m_sinks, sinkId, &SinkRegistration::id);
        if (found == m_sinks.end()) {
            throw NomadException("Unknown replacement log sink ID: " + std::to_string(sinkId.id));
        }
        if (std::ranges::count(sinkIds, sinkId) != 1) {
            throw NomadException("Duplicate replacement log sink ID: " + std::to_string(sinkId.id));
        }
        indices.push_back(static_cast<NomadIndex>(found - m_sinks.begin()));
    }
    flush();
    for (const auto index : indices) {
        replacement.push_back(std::move(m_sinks[index]));
        replacement.back().sink->setActive(true);
    }
    m_sinks.swap(replacement);
}

void Logger::clear() {
    const auto lock = std::lock_guard(m_mutex);
    m_entries.clear();
}

void Logger::flush() {
    const auto lock = std::lock_guard(m_mutex);
    requireIdleDelivery();
    std::vector<LogEntry> entries;
    entries.swap(m_entries);
    const auto sinkCount = m_sinks.size();
    m_delivering = true;
    try {
        for (auto index = NomadIndex{0}; index < sinkCount; ++index) {
            auto* sink = m_sinks[index].sink.get();
            if (!sink->getActive()) {
                continue;
            }
            sink->begin();
            for (const auto& entry : entries) {
                if (sink->getActive() && entry.level >= sink->getLogLevel()) {
                    sink->log(&entry);
                }
            }
            sink->end();
        }
    } catch (...) {
        m_delivering = false;
        throw;
    }
    m_delivering = false;
}

void Logger::requireIdleDelivery() const {
    if (m_delivering) {
        throw NomadException("Cannot recursively log, flush, remove, or replace sinks during log delivery");
    }
}

void Logger::log(const LogLevel level, const NomadStringView message, const Location& location) {
    const auto lock = std::lock_guard(m_mutex);
    if (level < m_logLevel.load()) {
        if (level >= LogLevel::Error) {
            flush();
        }
        return;
    }
    requireIdleDelivery();

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
        case LogLevel::Critical:
            logLevelName = "CRITICAL";
            break;
        default:
            logLevelName = "UNKNOWN";
            break;
    }

    auto time = std::chrono::system_clock::now();

    auto line = std::format(
        "{0:%F}T{0:%T} [{1}] {6} [{2}] [{3}:{4}:{5}]",
        time,
        logLevelName,
        location.function,
        location.file,
        location.line,
        location.column,
        message
    );

    m_entries.emplace_back(LogEntry{
        time,
        level,
        line
    });
    if (m_deliveryMode == LogDeliveryMode::Synchronous || level >= LogLevel::Error) {
        flush();
    }
}

namespace log {

Logger& getLogger() {
    static Logger logger;
    return logger;
}

void setLogLevel(const LogLevel level) {
    getLogger().setLogLevel(level);
}

LogLevel getLogLevel() {
    return getLogger().getLogLevel();
}

void setDeliveryMode(const LogDeliveryMode mode) {
    getLogger().setDeliveryMode(mode);
}

LogDeliveryMode getDeliveryMode() {
    return getLogger().getDeliveryMode();
}

void debug(const NomadStringView message, const Location& location) {
    getLogger().debug(message, location);
}

void info(const NomadStringView message, const Location& location) {
    getLogger().info(message, location);
}

void warn(const NomadStringView message, const Location& location) {
    getLogger().warning(message, location);
}

void warning(const NomadStringView message, const Location& location) {
    getLogger().warning(message, location);
}

void error(const NomadStringView message, const Location& location) {
    getLogger().error(message, location);
}

void fatal(const NomadStringView message, const Location& location) {
    getLogger().fatal(message, location);
}

void critical(const NomadStringView message, const Location& location) {
    getLogger().critical(message, location);
}

LogSinkId createConsoleSink(const ConsoleSinkConfiguration& configuration) {
    return getLogger().createConsoleSink(configuration);
}

LogSinkId createMemorySink(const MemorySinkConfiguration& configuration) {
    return getLogger().createMemorySink(configuration);
}

LogSinkId createSink(std::unique_ptr<LogSink> sink) {
    return getLogger().createSink(std::move(sink));
}

LogSink* getSink(const LogSinkId sinkId) {
    return getLogger().getSink(sinkId);
}

void removeSink(const LogSinkId sinkId) {
    getLogger().removeSink(sinkId);
}

void clearAllSinks() {
    getLogger().clearAllSinks();
}

void replaceSinks(const std::vector<LogSinkId>& sinkIds) {
    getLogger().replaceSinks(sinkIds);
}

void clear() {
    getLogger().clear();
}

void flush() {
    getLogger().flush();
}

} // log

} // nomad
