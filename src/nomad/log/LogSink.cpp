// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/LogSink.hpp>

namespace nomad {

LogSink::LogSink(const SinkConfiguration& configuration) :
    m_active(configuration.active),
    m_logLevel(configuration.level)
{
    setLogLevel(configuration.level);
}

bool LogSink::getActive() const {
    return m_active.load();
}

void LogSink::setActive(const bool active) {
    m_active.store(active);
}

LogLevel LogSink::getLogLevel() const {
    return m_logLevel.load();
}

void LogSink::setLogLevel(const LogLevel level) {
    if (!isValidLogLevel(level)) {
        throw NomadException("Invalid log sink level");
    }
    m_logLevel.store(level);
}

} // namespace nomad
