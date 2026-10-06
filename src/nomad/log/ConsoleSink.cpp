// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <iostream>

#include <nomad/log/ConsoleSink.hpp>

namespace nomad {

void ConsoleSink::log(const LogEntry* entry) {
    if (entry->level == LogLevel::Error || entry->level == LogLevel::Fatal) {
        std::cerr << entry->message << std::endl;
    } else {
        std::cout << entry->message << std::endl;
    }
}

void ConsoleSink::end() {
    std::cout.flush();
}

} // nomad
