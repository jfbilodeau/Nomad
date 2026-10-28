// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/LogSinkObject.hpp>

#include <nomad/log/Logger.hpp>

namespace nomad {

LogSinkObject::LogSinkObject(Logger& logger, const LogSinkId sinkId) :
    m_logger(logger),
    m_sinkId(sinkId)
{
    (void)getSink();
}

LogSinkId LogSinkObject::getSinkId() const {
    return m_sinkId;
}

LogSink& LogSinkObject::getSink() const {
    auto* sink = m_logger.getSink(m_sinkId);
    if (sink == nullptr) {
        throw NomadException("Unknown or removed log sink ID: " + std::to_string(m_sinkId.id));
    }
    return *sink;
}

} // namespace nomad
