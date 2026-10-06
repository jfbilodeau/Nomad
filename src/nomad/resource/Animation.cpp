// Copyright (c) 2024-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/resource/Animation.hpp>

#include <nomad/resource/ResourceManager.hpp>
#include <nomad/resource/Sprite.hpp>

#include <nomad/log/Logger.hpp>

#include <algorithm>

namespace nomad {

///////////////////////////////////////////////////////////////////////////////
// Animation
Animation::Animation(
    const NomadString& name,
    const NomadString& variant,
    const NomadString& direction,
    const std::vector<Sprite*>& frames
):
    m_name(name),
    m_variant(variant),
    m_direction(direction),
    m_frames(frames)
{}

bool Animation::nameIs(const NomadString& name, const NomadString& variant, const NomadString& direction) const {
    return
        m_name == name &&
        m_variant == variant &&
        m_direction == direction;
}

const NomadString& Animation::getName() const {
    return m_name;
}

const NomadString& Animation::getVariant() const {
    return m_variant;
}

const NomadString& Animation::getDirection() const {
    return m_direction;
}

NomadIndex Animation::getFrameCount() const {
    return m_frames.size();
}

const Sprite* Animation::getFrame(NomadIndex index) const {
    if (index >= m_frames.size()) {
        return nullptr;
    }

    return m_frames[index];
}

const Sprite* Animation::getLastFrame() const {
    return m_frames.back();
}

///////////////////////////////////////////////////////////////////////////////
// AnimationManager


AnimationManager::AnimationManager(ResourceManager* resources):
    m_resources(resources)
{}

NomadId AnimationManager::registerAnimation(
    const NomadString& name,
    const NomadString& variant,
    const NomadString& direction,
    const std::vector<Sprite*>& frames
) {
    auto animation_id = toNomadId(m_animations.size());

    m_animations.emplace_back(
        std::make_unique<Animation>(
            name,
            variant,
            direction,
            frames
        )
    );

    return animation_id;
}

const Animation* AnimationManager::getAnimation(
    const NomadString& name,
    const NomadString& variant,
    const NomadString& direction
) {
    // for (auto& animation : m_animations) {
    //     if (animation->nameIs(name, variant, direction)) {
    //         return animation.get();
    //     }
    // }
    auto it = std::ranges::find_if(
        m_animations,
        [&](const auto& animation) {
            return animation->nameIs(name, variant, direction);
        }
    );

    if (it != m_animations.end()) {
        return it->get();
    }

    // Animation does not exist. Attempt to create it.
    std::vector<Sprite*> frames;

    auto prefix = name + "-" + variant + "-" + direction + "-";

    m_resources->getSprites()->getSpritesByPrefix(prefix, frames);

    if (frames.empty()) {
        log::error("No frames found for animation prefix: " + prefix);

        return nullptr;
    }

    // Sort sprites by name.
    std::sort(frames.begin(), frames.end(), [](const Sprite* a, const Sprite* b) {
        return a->getName() < b->getName();
    });

    auto animation_id = registerAnimation(
        name,
        variant,
        direction,
        frames
    );

    return m_animations[animation_id].get();
}

} // nomad
