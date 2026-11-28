// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/script/NomadObject.hpp>

#include <memory>

namespace nomad {

class Scene;
class ScriptObject;

class SceneObject final : public NomadObject {
public:
    explicit SceneObject(Scene& scene);
    SceneObject(const SceneObject&) = delete;
    SceneObject& operator=(const SceneObject&) = delete;
    ~SceneObject() override;

    [[nodiscard]] Scene& getScene() const;
    [[nodiscard]] ScriptObject* asScriptObject() noexcept override;
    [[nodiscard]] const ScriptObject* asScriptObject() const noexcept override;
    void detachScene() noexcept;

private:
    Scene* m_scene;
    std::unique_ptr<ScriptObject, decltype(&nomad::releaseRef)> m_storage{nullptr, nomad::releaseRef};
};

} // namespace nomad
