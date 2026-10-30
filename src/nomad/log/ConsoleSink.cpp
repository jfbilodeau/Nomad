// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/ConsoleSink.hpp>

#include <iostream>

namespace nomad {

ConsoleSink::ConsoleSink(const ConsoleSinkConfiguration& configuration) : LogSink(configuration) {}

void ConsoleSink::log(const LogEntry* entry) {
    auto& output = entry->level >= LogLevel::Error ? std::cerr : std::cout;
    output << entry->message << std::endl;
    if (!output) {
        throw NomadException("Failed to write to the log console");
    }
}

void ConsoleSink::end() {
    std::cout.flush();
    std::cerr.flush();
    if (!std::cout || !std::cerr) {
        throw NomadException("Failed to flush the log console");
    }
}

} // nomad
