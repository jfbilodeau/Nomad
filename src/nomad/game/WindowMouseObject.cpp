// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/WindowMouseObject.hpp>

namespace nomad {

WindowMouseObject::WindowMouseObject(Game& game) : m_game(&game) {
}

Game& WindowMouseObject::getGame() const {
    if (m_game == nullptr) {
        throw NomadException("Window is no longer available");
    }

    return *m_game;
}

void WindowMouseObject::detachGame() noexcept {
    m_game = nullptr;
}

} // namespace nomad
