// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/LoggerObject.hpp>

namespace nomad {

LoggerObject::LoggerObject(Logger& logger) :
    m_logger(logger)
{
}

Logger& LoggerObject::getLogger() const {
    return m_logger;
}

} // namespace nomad
