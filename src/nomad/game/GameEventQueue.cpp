// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/GameEventQueue.hpp>

namespace nomad {

void GameEventQueue::push(GameEvent event) {
    m_events.push(std::move(event));
}

bool GameEventQueue::empty() const {
    return m_events.empty();
}

std::size_t GameEventQueue::size() const {
    return m_events.size();
}

} // namespace nomad
