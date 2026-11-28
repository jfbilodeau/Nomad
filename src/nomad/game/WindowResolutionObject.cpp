// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/WindowResolutionObject.hpp>

namespace nomad {

WindowResolutionObject::WindowResolutionObject(Game& game) : m_game(&game) {
}

Game& WindowResolutionObject::getGame() const {
    if (m_game == nullptr) {
        throw NomadException("Window is no longer available");
    }

    return *m_game;
}

void WindowResolutionObject::detachGame() noexcept {
    m_game = nullptr;
}

} // namespace nomad
