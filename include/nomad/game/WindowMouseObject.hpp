// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>

namespace nomad {

class Game;

class WindowMouseObject final : public NomadObject {
public:
    explicit WindowMouseObject(Game& game);
    WindowMouseObject(const WindowMouseObject&) = delete;
    WindowMouseObject& operator=(const WindowMouseObject&) = delete;
    ~WindowMouseObject() override = default;

    [[nodiscard]] Game& getGame() const;
    void detachGame() noexcept;

private:
    Game* m_game;
};

} // namespace nomad
