// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#include <nomad/game/Entity.hpp>

#include <nomad/game/Canvas.hpp>
#include <nomad/game/EventRegistration.hpp>
#include <nomad/game/Game.hpp>
#include <nomad/game/Scene.hpp>

#include <nomad/geometry/Intersection.hpp>

#include <nomad/script/Closure.hpp>
#include <nomad/script/Runtime.hpp>

#include <nomad/resource/Animation.hpp>
#include <nomad/resource/Font.hpp>
#include <nomad/resource/ResourceManager.hpp>
#include <nomad/resource/Sprite.hpp>

namespace nomad {

Entity::Entity(
    Scene* scene,
    const VariableMap* variableMap,
    const NomadId id,
    const NomadFloat x,
    const NomadFloat y,
    const NomadInteger layer
):
    m_id(id),
    m_scene(scene),
    m_position(x, y),
    m_previousPosition(x, y),
    m_layer(layer),
    m_variables(variableMap)
{
    m_name = "<entity" + toString(id) + ">";
}

Entity::~Entity() {
    invalidateTextTexture(); // Clear the text texture if it exists
}

void Entity::update(const Scene* scene) {
    const auto game = scene->getGame();

    // Run system functions
    executeSystemsBeforeUpdate();

    const auto updateFunctionId = m_oldEvents.getFunctionIdForSystemEvent(SystemEvent::Update);

    if (updateFunctionId != NOMAD_INVALID_ID) {
        game->executeFunction(updateFunctionId, m_scene, this);
    }

    // Run after update systems.
    executeSystemsAfterUpdate();

    // Update movement.
    if (m_moveToDestination) {
        const auto distance = m_position.distanceTo(m_destination);
        const auto speed = m_speed / static_cast<NomadFloat>(m_scene->getGame()->getFps());

        if (distance > speed) {
            const auto angle = m_position.angleTo(m_destination);

            const auto velocity_x = std::cos(angle) * speed;
            const auto velocity_y = std::sin(angle) * speed;

            setVelocity(velocity_x, velocity_y);
        } else {
            setLocation(m_destination);
            setVelocity(0, 0);
            m_moveToDestination = false;

            if (m_onArriveAtDestinationClosure != nullptr) {
                game->executeFunction(m_onArriveAtDestinationClosure.get(), m_scene, this);
            } else if (m_onArriveAtDestination != NOMAD_INVALID_ID) {
                game->executeFunction(m_onArriveAtDestination, m_scene, this);
            }
        }
    }

    // Do we need to select new animation?
    if (m_animationDirty) {
        m_animationDirty = false;

        auto animation = game->getResources()->getAnimations()->getAnimation(
            m_animationName.empty() ? m_name : m_animationName,
            m_animationVariant,
            m_animationDirection
            );

        if (animation == nullptr) {
            log::warning(
                "Animation '" + m_animationName + "_" + m_animationVariant + "_" + m_animationDirection +
                "' not found"
                );
        }

        setAnimation(animation);
    }

    // Update animation
    if (m_animation) {
        m_frameCount--;

        if (m_frameCount <= 0) {
            if (m_animationReverse) {
                m_currentFrame--;

                if (m_currentFrame <= 0) {
                    if (m_animationRepeat) {
                        m_currentFrame = static_cast<NomadInteger>(m_animation->getFrameCount()) - 1;
                    } else {
                        // Don't repeat, so set to first frame
                        setSprite(m_animation->getFrame(0));
                    }

                    if (m_onAnimationEndClosure != nullptr) {
                        game->executeFunction(m_onAnimationEndClosure.get(), m_scene, this);
                    } else if (m_onAnimationEnd != NOMAD_INVALID_ID) {
                        game->executeFunction(m_onAnimationEnd, m_scene, this);
                    }
                }
            } else {
                m_currentFrame++;

                if (m_currentFrame >= static_cast<NomadInteger>(m_animation->getFrameCount())) {
                    if (m_animationRepeat) {
                        m_currentFrame = 0;
                    } else {
                        // Don't repeat, so set to last frame
                        setSprite(m_animation->getFrame(m_animation->getFrameCount() - 1));
                    }

                    if (m_onAnimationEndClosure != nullptr) {
                        game->executeFunction(m_onAnimationEndClosure.get(), m_scene, this);
                    } else if (m_onAnimationEnd != NOMAD_INVALID_ID) {
                        game->executeFunction(m_onAnimationEnd, m_scene, this);
                    }
                }
            }

            // Update sprite if the animation has not been stopped
            if (m_animation) {
                m_frameCount = m_animationDuration;

                m_sprite = m_animation->getFrame(m_currentFrame);
            }
        }
    }
}

void Entity::render(Canvas* canvas) {
    if (m_visible == false) {
        return;
    }

    const NomadFloat entity_x = getX();
    const NomadFloat entity_y = getY();

    if (m_sprite != nullptr) {
        const auto sprite_x = entity_x - getSpriteX();
        const auto sprite_y = entity_y - getSpriteY();

        canvas->renderSprite(m_sprite, sprite_x, sprite_y, m_opacity);
    }

    if (m_textTexture == nullptr && !m_textValue.empty()) {
        generateTextTexture(canvas);
    }

    if (m_textTexture != nullptr) {
        PointF textAnchor;

        const auto textX = m_textPosition.getX();
        const auto textY = m_textPosition.getY();

        const auto textWidth = static_cast<Coord>(m_textTexture->getWidth());
        const auto textHeight = static_cast<Coord>(m_textTexture->getHeight());

        switch (m_textAlignment) {
        case Alignment::TopLeft:
            textAnchor.set(textX - textWidth, textY - textHeight);
            break;

        case Alignment::TopMiddle:
            textAnchor.set(textX - (textWidth / 2), textY - textHeight);
            break;

        case Alignment::TopRight:
            textAnchor.set(textX, textY - textHeight);
            break;

        case Alignment::CenterLeft:
            textAnchor.set(textX - textWidth, textY  - (textHeight / 2));
            break;

        case Alignment::CenterMiddle:
            textAnchor.set(textX - (textWidth / 2), textY - (textHeight / 2));
            break;

        case Alignment::CenterRight:
            textAnchor.set(textX, textY - (textHeight / 2));
            break;

        case Alignment::BottomLeft:
            textAnchor.set(textX - textWidth, textY);
            break;

        case Alignment::BottomMiddle:
            textAnchor.set(textX - (textWidth / 2), textY);
            break;

        case Alignment::BottomRight:
            textAnchor.set(textX, textY);
            break;

        default:
            log::warning(
                "Invalid text alignment " +
                toString(static_cast<int>(m_textAlignment)) +
                " for entity '" +
                m_name +
                "'"
                );
        }

        const auto source = RectangleF {
            0.0,
            0.0,
            textWidth,
            textHeight
        };

        const auto destination = RectangleF {
            textAnchor.getX() + entity_x + m_textPosition.getX(),
            textAnchor.getY() + entity_y + m_textPosition.getY(),
            textWidth,
            textHeight
        };

        canvas->renderTexture(m_textTexture, source, destination);
    }
}

NomadId Entity::getId() const {
    return m_id;
}

Scene* Entity::getScene() const {
    return m_scene;
}

void Entity::setName(const NomadString& name) {
    m_name = name;
}

const NomadString& Entity::getName() const {
    return m_name;
}

void Entity::setFunctionName(const NomadString& functionName) {
    m_functionName = functionName;
}

const NomadString& Entity::getFunctionName() const {
    return m_functionName;
}

void Entity::setX(const NomadFloat x) {
    m_position.setX(x);
    m_destination.setX(x);
    m_positionInvalidated = true;
}

NomadFloat Entity::getX() const {
    return m_position.getX();
}

void Entity::setY(NomadFloat y) {
    m_position.setY(y);
    m_destination.setY(y);
    m_positionInvalidated = true;
}

NomadFloat Entity::getY() const {
    return m_position.getY();
}

void Entity::setZ(NomadFloat z) {
    m_z = z;
}

NomadFloat Entity::getZ() const {
    return m_z;
}

void Entity::setLocation(NomadFloat x, NomadFloat y) {
    m_position.set(x, y);
    m_positionInvalidated = true;
}

void Entity::setLocation(const PointF& location) {
    m_position.set(location);
}

const PointF& Entity::getPosition() const {
    return m_position;
}

void Entity::setPreviousPosition(const PointF &position) {
    m_previousPosition = position;
}

void Entity::setPreviousX(NomadFloat x) {
    m_previousPosition.setX(x);
}

void Entity::setPreviousY(NomadFloat y) {
    m_previousPosition.setY(y);
}

const PointF & Entity::getPreviousPosition() const {
    return m_previousPosition;
}

NomadFloat Entity::getDeltaX() const {
    return m_position.getX() - m_previousPosition.getX();
}

NomadFloat Entity::getDeltaY() const {
    return m_position.getY() - m_previousPosition.getY();
}

NomadFloat Entity::getDistanceMoved() const {
    return m_position.distanceTo(m_previousPosition);
}

NomadBoolean Entity::hasMoved() const {
    return m_position != m_previousPosition;
}

void Entity::setSize(const NomadFloat width, const NomadFloat height) {
    m_size.set(width, height);
}

void Entity::setWidth(const NomadFloat width) {
    m_size.setX(width);
}

NomadFloat Entity::getWidth() const {
    return m_size.getX();
}

void Entity::setHeight(const NomadFloat height) {
    m_size.setY(height);
}

NomadFloat Entity::getHeight() const {
    return m_size.getY();
}

PointF Entity::getSize() const {
    return m_size;
}

void Entity::pause() {
    m_paused = true;

    // Invalidate velocity to ensure sprite stops moving while paused.
    m_velocityInvalidated = true;
}

void Entity::unpause() {
    m_paused = false;

    // Invalidate velocity to ensure sprite starts moving again.
    m_velocityInvalidated = true;
}

void Entity::setPause(bool pause) {
    m_paused = pause;
}

bool Entity::isPaused() const {
    return m_paused;
}

void Entity::hide() {
    m_visible = false;
}

void Entity::show() {
    m_visible = true;
}

void Entity::setVisible(bool visible) {
    m_visible = visible;
}

bool Entity::isVisible() const {
    return m_visible;
}

bool Entity::isHidden() const {
    return !m_visible;
}

void Entity::setOpacity(const NomadFloat opacity) {
    m_opacity = opacity;
}

NomadFloat Entity::getOpacity() const {
    return m_opacity;
}

void Entity::stopMoving() {
    setVelocity(0, 0);
    m_moveToDestination = false;
}

void Entity::move(const PointF& velocity) {
    setVelocity(velocity);
    m_moveToDestination = false;
}

void Entity::move(const NomadFloat x, const NomadFloat y) {
    setVelocity(x, y);
    m_moveToDestination = false;
}

void Entity::startMovingInDirection(const NomadFloat angle, const NomadFloat speed) {
    setVelocity(
        std::cos(angle) * speed,
        std::sin(angle) * speed
        );
    m_moveToDestination = false;
}

void Entity::moveTo(const PointF& destination, const NomadFloat speed, const NomadId onArriveAtDestination) {
    moveTo(destination.getX(), destination.getY(), speed, onArriveAtDestination);
}

void Entity::moveTo(const NomadFloat x, const NomadFloat y, const NomadFloat speed, const NomadId onArriveAtDestination) {
    m_destination.set(x, y);
    m_speed = speed;
    m_velocityInvalidated = true;
    m_moveToDestination = true;
    m_onArriveAtDestination = onArriveAtDestination;
    m_onArriveAtDestinationClosure.reset();
}

void Entity::moveTo(const PointF& destination, const NomadFloat speed, std::unique_ptr<Closure> onArriveAtDestination) {
    moveTo(destination.getX(), destination.getY(), speed, std::move(onArriveAtDestination));
}

void Entity::moveTo(const NomadFloat x, const NomadFloat y, const NomadFloat speed, std::unique_ptr<Closure> onArriveAtDestination) {
    m_destination.set(x, y);
    m_speed = speed;
    m_velocityInvalidated = true;
    m_moveToDestination = true;
    m_onArriveAtDestination = NOMAD_INVALID_ID;
    m_onArriveAtDestinationClosure = std::move(onArriveAtDestination);
}

bool Entity::isMoving() const {
    return m_velocity.is_zero();
}

void Entity::setVelocity(NomadFloat x, NomadFloat y) {
    setVelocity({x, y});
}

void Entity::setVelocity(const PointF& velocity) {
    if (m_velocity != velocity) {
        m_velocity = velocity;
        m_velocityInvalidated = true;
    }
}

void Entity::setVelocity(Cardinal direction, NomadFloat speed) {
    switch (direction) {
    case Cardinal::North:
        m_velocity.set(0.0, -speed);
        break;

    case Cardinal::East:
        m_velocity.set(speed, 0.0);
        break;

    case Cardinal::South:
        m_velocity.set(0.0, speed);
        break;

    case Cardinal::West:
        m_velocity.set(-speed, 0.0);
        break;

    case Cardinal::Unknown:
        m_velocity.set(0, 0);
        break;
    }
    m_velocityInvalidated = true;
}

void Entity::setVelocityX(NomadFloat x) {
    m_velocity.setX(x);
    m_velocityInvalidated = true;
}

void Entity::setVelocityY(NomadFloat y) {
    m_velocity.setY(y);
    m_velocityInvalidated = true;
}

const PointF& Entity::getVelocity() const {
    return m_velocity;
}

void Entity::setDestination(NomadFloat x, NomadFloat y) {
    m_destination.set(x, y);
}

void Entity::setDestination(const PointF& destination) {
    m_destination.set(destination);
}

const PointF& Entity::getDestination() const {
    return m_destination;
}

void Entity::setDestinationX(NomadFloat x) {
    m_destination.setX(x);
}

NomadFloat Entity::get_destination_x() const {
    return m_destination.getX();
}

void Entity::set_destination_y(NomadFloat y) {
    m_destination.setY(y);
}

NomadFloat Entity::getDestinationY() const {
    return m_destination.getY();
}

void Entity::setSpeed(NomadFloat speed) {
    m_speed = speed;
}

NomadFloat Entity::getSpeed() const {
    return m_speed;
}

void Entity::setMask(const NomadInteger mask) {
    //    m_mask = mask;
    m_mask = mask;
}

NomadInteger Entity::getMask() const {
    return m_mask;
}

void Entity::setCollisionMask(const NomadInteger collisionMask) {
    m_collisionMask = collisionMask;
}

[[nodiscard]] NomadInteger Entity::getCollisionMask() const {
    return m_collisionMask;
}

void Entity::setSensorMask(const NomadInteger sensorMask) {
    m_sensorMask = sensorMask;
}

NomadInteger Entity::getSensorMask() const {
    return m_sensorMask;
}

void Entity::setNoBody() {
    if (m_bodyShape == BodyShape::None) {
        return;
    }

    invalidatePhysicsBody();

    m_bodyShape = BodyShape::None;
}

void Entity::setCircleBody(const BodyType bodyType, const NomadFloat radius) {
    invalidatePhysicsBody();

    m_bodyShape = BodyShape::Circle;
    m_bodyType = bodyType;
    m_bodyRadius = radius;
}

void Entity::setRectangleBody(const BodyType bodyType, const NomadFloat width, const NomadFloat height) {
    invalidatePhysicsBody();

    m_bodyShape = BodyShape::Rectangle;
    m_bodyType = bodyType;
    m_bodyWidth = width;
    m_bodyHeight = height;
}

void Entity::destroyBody() {
    if (b2Body_IsValid(m_b2Body)) {
        b2DestroyBody(m_b2Body);
    }

    m_hasBody = false;
}

BodyType Entity::getBodyType() const {
    return m_bodyType;
}

BodyShape Entity::getBodyShape() const {
    return m_bodyShape;
}

NomadFloat Entity::getBodyWidth() const {
    return m_bodyWidth;
}

NomadFloat Entity::getBodyHeight() const {
    return m_bodyHeight;
}

NomadFloat Entity::getBodyRadius() const {
    return m_bodyRadius;
}

bool Entity::isTouching(const RectangleF& rectangle) const {
    if (m_bodyShape == BodyShape::Rectangle) {
        const auto entity_rectangle = RectangleF(
            m_position.getX() - m_bodyWidth / 2,
            m_position.getY() - m_bodyHeight / 2,
            m_bodyWidth,
            m_bodyHeight
            );

        return rectangleRectangleIntersect(entity_rectangle, rectangle);
    } else if (m_bodyShape == BodyShape::Circle) {
        const auto entity_circle = CircleF(
            m_position.getX(),
            m_position.getY(),
            m_bodyRadius
            );

        return circleRectangleIntersect(entity_circle, rectangle);
    }

    return false;
}

bool Entity::isTouching(const CircleF& circle) const {
    if (m_bodyShape == BodyShape::Rectangle) {
        const auto entity_rectangle = RectangleF(
            m_position.getX() - m_bodyWidth / 2,
            m_position.getY() - m_bodyHeight / 2,
            m_bodyWidth,
            m_bodyHeight
            );

        return circleRectangleIntersect(circle, entity_rectangle);
    } else if (m_bodyShape == BodyShape::Circle) {
        const auto entity_circle = CircleF(
            m_position.getX(),
            m_position.getY(),
            m_bodyRadius
            );

        return circleCircleIntersect(entity_circle, circle);
    }

    return false;
}

bool Entity::isTouching(const Entity* entity) const {
    const auto entity_body_shape = entity->getBodyShape();

    if (entity_body_shape == BodyShape::Rectangle) {
        const auto entity_rectangle = RectangleF(
            entity->getX() - entity->getBodyWidth() / 2,
            entity->getY() - entity->getBodyHeight() / 2,
            entity->getBodyWidth(),
            entity->getBodyHeight()
            );

        return isTouching(entity_rectangle);
    } else if (entity_body_shape == BodyShape::Circle) {
        const auto entity_circle = CircleF(
            entity->getX(),
            entity->getY(),
            entity->getBodyRadius()
            );

        return isTouching(entity_circle);
    }

    return false;
}

RectangleF& Entity::getBoundingBox(RectangleF &boundingBox) const {
    boundingBox = RectangleF{
        m_position.getX(),
        m_position.getY(),
        0.0f,
        0.0f
    };

    if (auto sprite = getSprite()) {
        boundingBox = sprite->getFrame().toRectangleF();
    }

    switch (m_bodyShape) {
    case BodyShape::None:
        // Ignore
        break;
    case BodyShape::Rectangle:
        boundingBox = boundingBox.unionRect({
            m_position.getX() - m_bodyWidth / 2,
            m_position.getY() - m_bodyHeight / 2,
            m_bodyWidth,
            m_bodyHeight
        });
        break;
    case BodyShape::Circle:
        boundingBox = boundingBox.unionRect({
            m_position.getX() - m_bodyWidth / 2,
            m_position.getY() - m_bodyHeight / 2,
            m_bodyRadius,
            m_bodyRadius
        });
        break;
    default:
        log::error("Invalid body shape: " + toString(static_cast<int>(m_bodyShape)));
    }

    return boundingBox;
}

void Entity::addSystemBeforeUpdate(NomadString systemId, NomadId functionId) {
    // Is system already registered?
    auto i = std::ranges::find_if(m_systemsBeforeUpdate, [systemId](auto& s) { return s.systemId == systemId; });

    if (i != m_systemsBeforeUpdate.end()) {
        // Already registered, update registration
        i->functionId = functionId;
    } else {
        // Let's register it.
        m_systemsBeforeUpdate.emplace_back(systemId, functionId);
    }
}

void Entity::addSystemAfterUpdate(NomadString systemId, NomadId functionId) {
    // Is system already registered?
    auto i = std::ranges::find_if(m_systemsAfterUpdate, [systemId](auto& s) { return s.systemId == systemId; });

    if (i != m_systemsAfterUpdate.end()) {
        // Already registered, update registration
        i->functionId = functionId;
    } else {
        // Let's register it.
        m_systemsAfterUpdate.emplace_back(systemId, functionId);
    }
}

void Entity::removeSystem(const NomadString& systemId) {
    std::erase_if(m_systemsBeforeUpdate, [systemId](auto& s) { return s.systemId == systemId; });
    std::erase_if(m_systemsAfterUpdate, [systemId](auto& s) { return s.systemId == systemId; });
}

void Entity::removeAllSystems() {
    m_systemsBeforeUpdate.clear();
    m_systemsAfterUpdate.clear();
}

void Entity::executeSystemsBeforeUpdate() {
    for (const auto& system : m_systemsBeforeUpdate) {
        m_scene->getGame()->executeFunction(system.functionId, m_scene, this);
    }
}

void Entity::executeSystemsAfterUpdate() {
    for (const auto& system : m_systemsAfterUpdate) {
        m_scene->getGame()->executeFunction(system.functionId, m_scene, this);
    }
}

void Entity::triggerBeginCollision(Entity* other, const NomadInteger mask) {
    const auto functionId = m_oldEvents.getFunctionIdForSystemEvent(SystemEvent::BeginCollision);

    if (functionId != NOMAD_INVALID_ID) {
        m_scene->getGame()->executeFunction(functionId, m_scene, this, other);
    }

    m_oldEvents.enumerateRegistrations([&](const EventRegistration& registration) {
        if (registration.eventId == SystemEvent::BeginCollisionWith && registration.mask & mask) {
            m_scene->getGame()->executeFunction(
                registration.functionId,
                m_scene,
                this,
                other
            );
        }
    });
}

void Entity::triggerEndCollision(Entity *other, const NomadInteger mask) {
    const auto functionId = m_oldEvents.getFunctionIdForSystemEvent(SystemEvent::EndCollision);

    if (functionId != NOMAD_INVALID_ID) {
        m_scene->getGame()->executeFunction(functionId, m_scene, this, other);
    }

    m_oldEvents.enumerateRegistrations([&](const EventRegistration& registration) {
        if (registration.eventId == SystemEvent::EndCollisionWith && registration.mask & mask) {
            m_scene->getGame()->executeFunction(
                registration.functionId,
                m_scene,
                this,
                other
                );
        }
    });
}

void Entity::enterCamera() {
    if (m_inCamera) {
        return;
    }

    const auto functionId = m_oldEvents.getFunctionIdForSystemEvent(SystemEvent::EnterCamera);

    if (functionId != NOMAD_INVALID_ID) {
        m_scene->getGame()->executeFunction(functionId, m_scene, this);
    }
}

void Entity::exitCamera() {
    if (!m_inCamera) {
        return;
    }

    auto functionId = m_oldEvents.getFunctionIdForSystemEvent(SystemEvent::ExitCamera);

    if (functionId != NOMAD_INVALID_ID) {
        m_scene->getGame()->executeFunction(functionId, m_scene, this);
    }
}

void Entity::setOnEnterCamera(NomadId functionId) {
    m_oldEvents.registerSystemEvent(SystemEvent::EnterCamera, functionId);
}

NomadId Entity::getOnEnterCamera() const {
    return m_oldEvents.getFunctionIdForSystemEvent(SystemEvent::EnterCamera);
}

void Entity::setOnExitCamera(NomadId functionId) {
    m_oldEvents.registerSystemEvent(SystemEvent::ExitCamera, functionId);
}

NomadId Entity::getOnExitCamera() const {
    return m_oldEvents.getFunctionIdForSystemEvent(SystemEvent::ExitCamera);
}

bool Entity::isInCamera() const {
    return m_inCamera;
}

void Entity::invalidatePhysicsBody() {
    m_bodyInvalidated = true;
}

void Entity::beforeSimulationUpdate(const b2WorldId world) {
    if (m_bodyInvalidated) {
        if (m_hasBody) {
            destroyBody();
        }

        m_bodyInvalidated = false;

        if (m_bodyShape != BodyShape::None) {
            generateBody(world);

            // Re-initialize position and velocity
            m_positionInvalidated = true;
            m_velocityInvalidated = true;

            m_hasBody = true;
        }
    }

    if (m_hasBody) {
        if (m_positionInvalidated) {
            // Make sure body is at the same position as the entity
            b2Body_SetTransform(
                m_b2Body,
                b2Vec2{
                    static_cast<float>(m_position.getX()),
                    static_cast<float>(m_position.getY())
                }, b2Rot_identity
            );
        }

        if  (m_velocityInvalidated) {
            b2Vec2 velocity;

            if (!m_paused) {
                velocity.x = static_cast<float>(m_velocity.getX());
                velocity.y = static_cast<float>(m_velocity.getY());
            } else {
                velocity.x = 0;
                velocity.y = 0;
            }

            b2Body_SetLinearVelocity(m_b2Body, velocity);
        }
    }

    m_positionInvalidated = false;
    m_velocityInvalidated = false;
}

void Entity::afterSimulationUpdate(b2WorldId /*world*/) {
    // Update entity position based on physics body.
    if (m_hasBody) {
        auto position = b2Body_GetPosition(m_b2Body);
        m_position.set(position.x, position.y);

        // auto contactCount = b2Shape_GetSensorCapacity(m_b2Shape);
        //
        // if (contactCount > 0) {
        //     log::info("Contact count: " + toString(contactCount));
        // }

    } else {
        // When no physics body, manually update velocity.
        m_position.translate(m_velocity);
    }
}

void Entity::setLayer(const NomadInteger layer) {
    m_layer = layer;
    invalidatePhysicsBody();
}

NomadInteger Entity::getLayer() const {
    return m_layer;
}

void Entity::setSpriteName(const NomadString& spriteName) {
    const auto sprite = m_scene->getGame()->getResources()->getSprites()->getSpriteByName(spriteName);

    if (sprite == nullptr) {
        log::warning("Sprite '" + spriteName + "' not found");
        return;
    }

    setSprite(sprite);
}

const NomadString& Entity::getSpriteName() const {
    if (m_sprite == nullptr) {
        return NOMAD_EMPTY_STRING;
    }

    return m_sprite->getName();
}

void Entity::setSprite(const Sprite* sprite) {
    // Disable animation.
    m_animation = nullptr;

    if (sprite == m_sprite) {
        return;
    }

    m_sprite = sprite;
}

const Sprite* Entity::getSprite() const {
    return m_sprite;
}

void Entity::setSpriteX(NomadFloat x) {
    m_spriteAnchor.setX(x);
}

NomadFloat Entity::getSpriteX() const {
    return m_spriteAnchor.getX();
}

void Entity::setSpriteY(NomadFloat y) {
    m_spriteAnchor.setY(y);
}

NomadFloat Entity::getSpriteY() const {
    return m_spriteAnchor.getY();
}

void Entity::setSpriteAnchor(const PointF& anchor) {
    m_spriteAnchor = anchor;
}

void Entity::setSpriteAnchor(NomadFloat x, NomadFloat y) {
    m_spriteAnchor.set(x, y);
}

const PointF& Entity::getSpriteAnchor() const {
    return m_spriteAnchor;
}

void Entity::setAnimation(const Animation* animation) {
    if (animation == m_animation) {
        return;
    }

    m_animation = animation;
    m_frameCount = 0;

    if (m_animation == nullptr) {
        setSprite(nullptr);
        return;
    }

    if (m_animationReverse) {
        m_sprite = m_animation->getLastFrame();
    } else {
        m_sprite = m_animation->getFrame(0);
    }
}

[[nodiscard]] const Animation* Entity::getAnimation() const {
    return m_animation;
}

void Entity::setAnimationName(const NomadString& animationName) {
    if (animationName == m_animationName) {
        return;
    }

    m_animationName = animationName;
    m_animationDirty = true;
}

const NomadString& Entity::getAnimationName() const {
    return m_animationName;
}

void Entity::setAnimationVariant(const NomadString& animationVariant) {
    if (animationVariant == m_animationVariant) {
        return;
    }

    m_animationVariant = animationVariant;
    m_animationDirty = true;
}

const NomadString& Entity::getAnimationVariant() const {
    return m_animationVariant;
}

void Entity::setAnimationDirection(const NomadString& animationDirection) {
    if (animationDirection == m_animationDirection) {
        return;
    }

    m_animationDirection = animationDirection;
    m_animationDirty = true;
}

const NomadString& Entity::getAnimationDirection() const {
    return m_animationDirection;
}

void Entity::setAnimationDuration(NomadInteger speed) {
    if (speed == m_animationDuration) {
        return;
    }

    m_animationDuration = speed;
}

NomadInteger Entity::getAnimationDuration() const {
    return m_animationDuration;
}

void Entity::setAnimationRepeat(bool repeat) {
    if (repeat == m_animationRepeat) {
        return;
    }

    m_animationRepeat = repeat;
}

bool Entity::getAnimationRepeat() const {
    return m_animationRepeat;
}

void Entity::setAnimationReverse(bool reverse) {
    if (reverse == m_animationReverse) {
        return;
    }

    m_animationReverse = reverse;
}

bool Entity::getAnimationReverse() const {
    return m_animationReverse;
}

void Entity::setOnAnimationEnd(const NomadId functionId) {
    m_onAnimationEnd = functionId;
    m_onAnimationEndClosure.reset();
}

void Entity::setOnAnimationEnd(std::unique_ptr<Closure> closure) {
    m_onAnimationEnd = NOMAD_INVALID_ID;
    m_onAnimationEndClosure = std::move(closure);
}

NomadId Entity::getOnAnimationEnd() const {
    return m_onAnimationEnd;
}

void Entity::playAnimation(
    const NomadString& variant,
    const NomadInteger duration,
    const NomadBoolean repeat,
    const NomadBoolean reverse,
    const NomadId onAnimationEnd
    ) {
    m_animationVariant = variant;
    m_animationDuration = duration;
    m_animationRepeat = repeat;
    m_animationReverse = reverse;
    m_onAnimationEnd = onAnimationEnd;
    m_onAnimationEndClosure.reset();

    m_animationDirty = true;
}

void Entity::playAnimation(
    const NomadString& variant,
    const NomadInteger duration,
    const NomadBoolean repeat,
    const NomadBoolean reverse,
    std::unique_ptr<Closure> onAnimationEnd
    ) {
    m_animationVariant = variant;
    m_animationDuration = duration;
    m_animationRepeat = repeat;
    m_animationReverse = reverse;
    m_onAnimationEnd = NOMAD_INVALID_ID;
    m_onAnimationEndClosure = std::move(onAnimationEnd);

    m_animationDirty = true;
}

void Entity::setText(const NomadString& text) {
    if (text != m_textValue) {
        m_textValue = text;

        invalidateTextTexture();
    }
}

const NomadString& Entity::getText() {
    return m_textValue;
}

void Entity::setTextAlignment(Alignment alignment) {
    m_textAlignment = alignment;
}

Alignment Entity::getTextAlignment() const {
    return m_textAlignment;
}

void Entity::setTextPosition(NomadFloat x, NomadFloat y) {
    m_textPosition = { x, y };
}

void Entity::setTextPosition(const PointF &position) {
    m_textPosition = position;
}

const PointF & Entity::getTextPosition() const {
    return m_textPosition;
}

void Entity::setTextX(NomadFloat x) {
    m_textPosition.setX(x);
}

NomadFloat Entity::getTextX() const {
    return m_textPosition.getX();
}

void Entity::setTextY(NomadFloat y) {
    m_textPosition.setY(y);
}

NomadFloat Entity::getTextY() const {
    return m_textPosition.getY();
}

void Entity::setTextWidth(NomadFloat width) {
    if (width == m_textWidth) {
        return;
    }

    m_textWidth = width;

    invalidateTextTexture();
}

NomadFloat Entity::getTextWidth() const {
    return m_textWidth;
}

void Entity::setTextHeight(NomadFloat height) {
    if (height == m_textHeight) {
        return;
    }

    m_textHeight = height;

    invalidateTextTexture();
}

NomadFloat Entity::getTextHeight() const {
    return m_textHeight;
}

void Entity::setTextLineSpacing(NomadFloat lineSpacing) {
    if (lineSpacing == m_textLineSpacing) {
        return;
    }

    m_textLineSpacing = lineSpacing;

    invalidateTextTexture();
}

NomadFloat Entity::getTextLineSpacing() const {
    return m_textLineSpacing;
}

void Entity::setTextColor(const Color& color) {
    if (color == m_textColor) {
        return;
    }

    m_textColor = color;

    invalidateTextTexture();
}

Color Entity::getTextColor() const {
    return m_textColor;
}

void Entity::setFontById(const NomadId fontId) {
    if (fontId == m_textFontId) {
        return;
    }

    m_textFontId = fontId;

    invalidateTextTexture();
}

NomadId Entity::getFontId() const {
    return m_textFontId;
}

void Entity::setVariableValue(const NomadId variableId, const RuntimeValue& value) {
    m_variables.setVariableValue(variableId, value);
}

void Entity::getVariableValue(const NomadId variableId, RuntimeValue& value) const {
    m_variables.getVariableValue(variableId, value);
}

void Entity::registerUserEvent(const NomadString& name, const NomadId functionId) {
    m_oldEvents.registerUserEvent(name, functionId);
}

void Entity::unregisterUserEvent(const NomadString& name) {
    m_oldEvents.unregisterUserEvent(name);
}

void Entity::triggerUserEvent(const NomadString &name) {
    const auto functionId = m_oldEvents.getFunctionIdForUserEvent(name);

    if (functionId != NOMAD_INVALID_ID) {
        m_scene->getGame()->executeFunction(functionId, m_scene, this);
    }
}

void Entity::registerEvent(NomadId eventId, std::unique_ptr<Closure> closure) {
    if (eventId == NOMAD_INVALID_ID) return;
    if (closure == nullptr) return;

    // Update event if already defined.
    for (auto& registration : m_eventRegistrations) {
        if (registration.eventId == eventId) {
            // Update existing registration
            registration.closure = std::move(closure);
            return;
        }
    }

    // Event not registered.
    m_eventRegistrations.push_back({
        eventId,
        std::move(closure)
    });
}

void Entity::unregisterEvent(NomadId eventId) {
    if (eventId == NOMAD_INVALID_ID) return;

    std::erase_if(
        m_eventRegistrations,
        [eventId](const NewEventRegistration& registration) {
            return registration.eventId == eventId;
        }
    );
}

void Entity::dispatchEvent(const EventDispatch& dispatch) {
    if (dispatch.entityId != m_id && dispatch.entityId != NOMAD_INVALID_ID) return;

    if (dispatch.eventId == NOMAD_INVALID_ID) {
        return;
    }

    const NewEventRegistration* callback = nullptr;

    for (const auto& registration : m_eventRegistrations) {
        if (registration.eventId == dispatch.eventId) {
            callback = &registration;
            break;
        }
    }

    if (callback == nullptr || callback->closure == nullptr) {
        return;
    }

    const auto function = callback->closure->getFunction();

    if (function == nullptr) {
        log::error("Unable to resolve event callback function for event " + toString(dispatch.eventId));
        return;
    }

    m_scene->getGame()->executeFunction(callback->closure.get(), m_scene, this, {}, dispatch.arguments);
}

NomadId Entity::getOnUpdate() const {
    return m_oldEvents.getFunctionIdForSystemEvent(SystemEvent::Update);
}

void Entity::setOnUpdate(const NomadId functionId) {
    m_oldEvents.registerSystemEvent(SystemEvent::Update, functionId);
}

void Entity::setOnBeginCollision(const NomadId functionId) {
    m_oldEvents.registerSystemEvent(SystemEvent::BeginCollision, functionId);
}

NomadId Entity::getOnBeginCollision() const {
    return m_oldEvents.getFunctionIdForSystemEvent(SystemEvent::BeginCollisionWith);
}

void Entity::setOnBeginCollisionWith(const NomadInteger mask, const NomadId functionId) {
    m_oldEvents.registerMaskEvent(SystemEvent::BeginCollisionWith, functionId, mask);
}

NomadId Entity::getOnBeginCollisionWith(const NomadInteger mask) const {
    return m_oldEvents.getFunctionIdForMaskEvent(SystemEvent::BeginCollisionWith, mask);
}

void Entity::setOnEndCollision(const NomadId functionId) {
    m_oldEvents.registerSystemEvent(SystemEvent::EndCollision, functionId);
}

NomadId Entity::getOnEndCollision() const {
    return m_oldEvents.getFunctionIdForSystemEvent(SystemEvent::EndCollision);
}

void Entity::setOnEndCollisionWith(const NomadInteger mask, const NomadId functionId) {
    m_oldEvents.registerMaskEvent(SystemEvent::EndCollisionWith, functionId, mask);
}

NomadId Entity::getOnEndCollisionWith(const NomadInteger mask) const {
    return m_oldEvents.getFunctionIdForMaskEvent(SystemEvent::EndCollisionWith, mask);
}

void Entity::invalidateTextTexture() {
    if (m_textTexture) {
        delete m_textTexture;

        m_textTexture = nullptr;
    }
}

void Entity::generateTextTexture(Canvas* canvas) {
    if (m_textTexture) {
        delete m_textTexture;

        m_textTexture = nullptr;
    }

    if (m_textFontId == NOMAD_INVALID_ID) {
        log::warning("No font set for entity '" + m_name + "'");
    } else {
        const auto font = canvas->getGame()->getResources()->getFonts()->getFont(m_textFontId);

        m_textTexture = font->generateTexture(
            canvas,
            m_textValue,
            m_textColor,
            getHorizontalAlignment(m_textAlignment),
            static_cast<NomadInteger>(m_textWidth),
            static_cast<NomadInteger>(m_textHeight),
            m_textLineSpacing
            );
    }
}

void Entity::generateBody(const b2WorldId world) {
    auto bodyDef = b2DefaultBodyDef();
    bodyDef.userData = this;

    if (m_bodyType == BodyType::Static) {
        bodyDef.type = b2_staticBody;
    } else if (m_bodyType == BodyType::Dynamic) {
        bodyDef.type = b2_dynamicBody;
    } else if (m_bodyType == BodyType::Kinematic) {
        bodyDef.type = b2_kinematicBody;
    } else {
        log::error("Invalid body type: " + toString(static_cast<int>(m_bodyType)));
        return;
    }

    // Create collision body.
    m_b2Body = b2CreateBody(world, &bodyDef);

    if (!b2Body_IsValid(m_b2Body)) {
        log::error("Failed to create body");
        return;
    }

    if (m_collisionMask) {
        m_b2Shape = createShape(false);
    }

    if (m_sensorMask) {
        m_b2SensorShape = createShape(true);
    }
}

b2ShapeId Entity::createShape(const bool sensor) {
    b2ShapeDef shapeDef = b2DefaultShapeDef();

    std::uint64_t mask;

    const std::uint64_t categoryMask = m_mask; // All categories

    if (sensor) {
        mask = m_sensorMask;
    } else {
        mask = m_collisionMask;
    }

    shapeDef.userData = this;
    shapeDef.isSensor = sensor;
    shapeDef.enableSensorEvents = true;
    shapeDef.filter = {
        categoryMask,
        mask,
        0
    };

    b2ShapeId shapeId;

    const auto bodyShape = m_bodyShape;

    if (bodyShape == BodyShape::Rectangle) {
        const auto halfWidth = static_cast<float>(m_bodyWidth) / 2.0f;
        const auto halfHeight = static_cast<float>(m_bodyHeight) / 2.0f;
        // Why do I need to flip width and height here?
        const b2Polygon rectangle = b2MakeOffsetBox(
            halfWidth,
            halfHeight,
            {
                halfWidth,
                halfHeight,
            },
            b2Rot_identity
        );
        shapeId = b2CreatePolygonShape(m_b2Body, &shapeDef, &rectangle);
    } else if (bodyShape == BodyShape::Circle) {
        const b2Circle circle = {
            {0.0f, 0.0f},
            static_cast<float>(m_bodyRadius)
        };
        shapeId = b2CreateCircleShape(m_b2Body, &shapeDef, &circle);
    } else {
        log::error("Invalid body shape: " + toString(static_cast<int>(bodyShape)));
        return {};
    }

    if (b2Shape_IsValid(shapeId) == false) {
        log::error("Failed to create shape");
        return {};
    }

    return shapeId;
}

} // namespace nomad
