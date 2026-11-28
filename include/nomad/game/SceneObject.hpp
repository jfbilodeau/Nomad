// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>

namespace nomad {

class Scene;

class SceneObject final : public NomadObject {
public:
    explicit SceneObject(Scene& scene);
    SceneObject(const SceneObject&) = delete;
    SceneObject& operator=(const SceneObject&) = delete;
    ~SceneObject() override = default;

    [[nodiscard]] Scene& getScene() const;
    void detachScene() noexcept;

private:
    Scene* m_scene;
};

} // namespace nomad
