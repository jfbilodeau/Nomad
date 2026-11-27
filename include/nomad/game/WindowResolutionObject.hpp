// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>

namespace nomad {

class Game;

class WindowResolutionObject final : public NomadObject {
public:
    explicit WindowResolutionObject(Game& game);
    WindowResolutionObject(const WindowResolutionObject&) = delete;
    WindowResolutionObject& operator=(const WindowResolutionObject&) = delete;
    ~WindowResolutionObject() override = default;

    [[nodiscard]] Game& getGame() const;
    void detachGame() noexcept;

private:
    Game* m_game;
};

} // namespace nomad
