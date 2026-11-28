// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/EntityObject.hpp>

#include <nomad/game/Entity.hpp>
#include <nomad/game/Game.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/script/ClassType.hpp>
#include <nomad/script/Property.hpp>
#include <nomad/script/Runtime.hpp>
#include <nomad/script/ScriptObject.hpp>

namespace nomad {

EntityObject::EntityObject(Entity& entity) : m_entity(&entity) {
    auto* scene = entity.getScene();
    auto* game = scene != nullptr ? scene->getGame() : nullptr;
    if (game == nullptr) {

        return;
    }

    const auto* type = game->getRuntime()->getTypeByName("game.Entity");
    const auto* classType = type != nullptr ? type->asClassType() : nullptr;
    if (classType == nullptr) {
        throw NomadException("Entity field storage requires the registered game.Entity class");
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

EntityObject::~EntityObject() = default;

Entity& EntityObject::getEntity() const {
    if (m_entity == nullptr) {
        throw NomadException("Entity is no longer available");
    }

    return *m_entity;
}

ScriptObject* EntityObject::asScriptObject() noexcept {

    return m_storage.get();
}

const ScriptObject* EntityObject::asScriptObject() const noexcept {

    return m_storage.get();
}

void EntityObject::detachEntity() noexcept {
    m_entity = nullptr;
    m_storage.reset();
}

} // namespace nomad
