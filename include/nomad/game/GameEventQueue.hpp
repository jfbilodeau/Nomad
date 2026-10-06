// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/game/GameEvent.hpp>

#include <cstddef>
#include <queue>
#include <utility>

namespace nomad {

class GameEventQueue final {
public:
    GameEventQueue() = default;
    GameEventQueue(const GameEventQueue&) = delete;
    GameEventQueue& operator=(const GameEventQueue&) = delete;

    void push(GameEvent event);

    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::size_t size() const;

    template<typename Handler>
    void processFrame(Handler&& handler) {
        const auto frameEventCount = m_events.size();

        for (std::size_t eventIndex = 0; eventIndex < frameEventCount; ++eventIndex) {
            auto event = std::move(m_events.front());
            m_events.pop();

            if (!handler(event)) {
                m_events.push(std::move(event));
            }
        }
    }

private:
    std::queue<GameEvent> m_events;
};

} // namespace nomad
