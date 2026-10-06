// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/log/MemorySink.hpp>

namespace nomad {

void MemorySink::log(const LogEntry* entry) {
    m_entries.push_back(*entry);
}

const std::vector<LogEntry>& MemorySink::getEntries() const {
    return m_entries;
}

void MemorySink::clear() {
    m_entries.clear();
}

} // nomad
