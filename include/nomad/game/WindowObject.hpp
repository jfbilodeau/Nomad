// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>

#include <memory>

namespace nomad {

class Game;
class WindowMouseObject;
class WindowResolutionObject;

class WindowObject final : public NomadObject {
public:
    explicit WindowObject(Game& game);
    WindowObject(const WindowObject&) = delete;
    WindowObject& operator=(const WindowObject&) = delete;
    ~WindowObject() override;

    [[nodiscard]] Game& getGame() const;
    [[nodiscard]] WindowMouseObject* getMouse() const;
    [[nodiscard]] WindowResolutionObject* getResolution() const;
    void detachGame() noexcept;

private:
    Game* m_game;
    std::unique_ptr<WindowMouseObject, decltype(&nomad::releaseRef)> m_mouse;
    std::unique_ptr<WindowResolutionObject, decltype(&nomad::releaseRef)> m_resolution;
};

} // namespace nomad
