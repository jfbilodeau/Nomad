// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/WindowObject.hpp>

#include <nomad/game/WindowMouseObject.hpp>
#include <nomad/game/WindowResolutionObject.hpp>

namespace nomad {

WindowObject::WindowObject(Game& game) :
    m_game(&game),
    m_mouse(new WindowMouseObject(game), nomad::releaseRef),
    m_resolution(new WindowResolutionObject(game), nomad::releaseRef) {
}

WindowObject::~WindowObject() = default;

Game& WindowObject::getGame() const {
    if (m_game == nullptr) {
        throw NomadException("Window is no longer available");
    }

    return *m_game;
}

WindowMouseObject* WindowObject::getMouse() const {
    (void)getGame();

    return m_mouse.get();
}

WindowResolutionObject* WindowObject::getResolution() const {
    (void)getGame();

    return m_resolution.get();
}

void WindowObject::detachGame() noexcept {
    m_game = nullptr;
    m_mouse->detachGame();
    m_resolution->detachGame();
}

} // namespace nomad
