// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/SceneObject.hpp>

namespace nomad {

SceneObject::SceneObject(Scene& scene) : m_scene(&scene) {
}

Scene& SceneObject::getScene() const {
    if (m_scene == nullptr) {
        throw NomadException("Scene is no longer available");
    }

    return *m_scene;
}

void SceneObject::detachScene() noexcept {
    m_scene = nullptr;
}

} // namespace nomad
