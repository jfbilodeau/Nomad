// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>

#include <memory>

namespace nomad {

class Entity;
class ScriptObject;

class EntityObject final : public NomadObject {
public:
    explicit EntityObject(Entity& entity);
    EntityObject(const EntityObject&) = delete;
    EntityObject& operator=(const EntityObject&) = delete;
    ~EntityObject() override;

    [[nodiscard]] Entity& getEntity() const;
    [[nodiscard]] ScriptObject* asScriptObject() noexcept override;
    [[nodiscard]] const ScriptObject* asScriptObject() const noexcept override;
    void detachEntity() noexcept;

private:
    Entity* m_entity;
    std::unique_ptr<ScriptObject, decltype(&nomad::releaseRef)> m_storage{nullptr, nomad::releaseRef};
};

} // namespace nomad
