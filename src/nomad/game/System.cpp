// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/System.hpp>

#include <utility>

namespace nomad {

nomad::System::System(std::shared_ptr<Logger> logger):
    m_logger(std::move(logger))
{
    m_logger->debug("Initializing system");
}

System::~System() {

}

std::shared_ptr<Logger> System::getLogger() const {
    return m_logger;
}

} // nomad
