// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/SceneObject.hpp>

#include <nomad/game/Game.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Property.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>

namespace nomad {

SceneObject::SceneObject(Scene& scene) : m_scene(&scene) {
    auto* game = scene.getGame();
    if (game == nullptr) {

        return;
    }

    const auto* type = game->getRuntime()->getTypeByName("game.Scene");
    const auto* classType = type != nullptr ? type->asClassType() : nullptr;
    if (classType == nullptr) {
        throw NomadException("Scene field storage requires the registered game.Scene class");
    }

    m_storage.reset(new ScriptObject(classType));
    auto fieldIndex = NomadIndex{0};
    for (const auto* property : classType->getProperties()) {
        if (property->asNativeProperty() != nullptr) {
            continue;
        }

        const auto field = m_storage->getFieldStorage(fieldIndex++);
        if (!field.empty()) {
            property->getValueType()->initValue(field.data());
        }
    }
}

SceneObject::~SceneObject() = default;

Scene& SceneObject::getScene() const {
    if (m_scene == nullptr) {
        throw NomadException("Scene is no longer available");
    }

    return *m_scene;
}

ScriptObject* SceneObject::asScriptObject() noexcept {

    return m_storage.get();
}

const ScriptObject* SceneObject::asScriptObject() const noexcept {

    return m_storage.get();
}

void SceneObject::detachScene() noexcept {
    m_scene = nullptr;
    m_storage.reset();
}

} // namespace nomad
