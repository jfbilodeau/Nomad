// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <vector>

namespace nomad {

class Sprite;
class ResourceManager;

///////////////////////////////////////////////////////////////////////////////
// Animation
///////////////////////////////////////////////////////////////////////////////
class Animation final {
public:
    Animation(
        const NomadString& name,
        const NomadString& variant,
        const NomadString& direction,
        const std::vector<Sprite*>& frames
    );
    Animation(const Animation& other) = delete;

    [[nodiscard]] bool nameIs(const NomadString& name, const NomadString& variant, const NomadString& direction) const;
    [[nodiscard]] const NomadString& getName() const;
    [[nodiscard]] const NomadString& getVariant() const;
    [[nodiscard]] const NomadString& getDirection() const;
    [[nodiscard]] NomadIndex getFrameCount() const;
    [[nodiscard]] const Sprite* getFrame(NomadIndex index) const;
    [[nodiscard]] const Sprite* getLastFrame() const;

private:
    NomadString m_name;
    NomadString m_variant;
    NomadString m_direction;
    std::vector<Sprite*> m_frames;
};

///////////////////////////////////////////////////////////////////////////////
// AnimationManager
///////////////////////////////////////////////////////////////////////////////
class AnimationManager {
public:
    explicit AnimationManager(ResourceManager* resources);
    AnimationManager(const AnimationManager& other) = delete;
    AnimationManager(AnimationManager&& other) noexcept = delete;
    AnimationManager& operator=(const AnimationManager& other) = delete;

    NomadId registerAnimation(
        const NomadString& name,
        const NomadString& variant,
        const NomadString& direction,
        const std::vector<Sprite*>& frames
    );
    [[nodiscard]] const Animation* getAnimation(const NomadString& name, const NomadString& variant, const NomadString& direction);

private:
    ResourceManager* m_resources;
    std::vector<std::unique_ptr<Animation>> m_animations;
};

} // nomad
