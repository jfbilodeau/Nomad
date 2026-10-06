// Copyright (c) 2025-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/game/Alignment.hpp>
#include <nomad/game/Body.hpp>
#include <nomad/game/Cardinal.hpp>
#include <nomad/game/Color.hpp>
#include <nomad/game/EventRegistration.hpp>
#include <nomad/game/GameExecutionContext.hpp>

#include <nomad/geometry/PointF.hpp>
#include <nomad/geometry/RectangleF.hpp>

#include <box2d/box2d.h>

#include <memory>

namespace nomad {

// Forward declarations
class Animation;
class Canvas;
class Closure;
class Game;
class Scene;
class Sprite;
class Texture;

class Entity {
public:
    explicit Entity(Scene* scene, const VariableMap* variableMap, NomadId id, NomadFloat x, NomadFloat y, NomadInteger layer);
    Entity(const Entity& other) = delete;
    ~Entity();

//    void init(Game* game, NomadId init_function_id);
    void update(const Scene* scene);
    void render(Canvas* canvas);

    [[nodiscard]] NomadId getId() const;

    [[nodiscard]] Scene* getScene() const;

    void setName(const NomadString& name);
    [[nodiscard]] const NomadString& getName() const;

    void setFunctionName(const NomadString& functionName);
    [[nodiscard]] const NomadString& getFunctionName() const;

    void setX(NomadFloat x);
    [[nodiscard]] NomadFloat getX() const;

    void setY(NomadFloat y);
    [[nodiscard]] NomadFloat getY() const;

    void setZ(NomadFloat z);
    [[nodiscard]] NomadFloat getZ() const;

    void setLocation(NomadFloat x, NomadFloat y);
    void setLocation(const PointF& location);
    [[nodiscard]] const PointF& getPosition() const;

    void setPreviousPosition(const PointF& position);
    void setPreviousX(NomadFloat x);
    void setPreviousY(NomadFloat y);
    [[nodiscard]] const PointF& getPreviousPosition() const;

    [[nodiscard]] NomadFloat getDeltaX() const;
    [[nodiscard]] NomadFloat getDeltaY() const;
    [[nodiscard]]
    NomadFloat getDistanceMoved() const;
    [[nodiscard]]
    NomadBoolean hasMoved() const;

    void setSize(NomadFloat width, NomadFloat height);
    void setWidth(NomadFloat width);
    [[nodiscard]]
    NomadFloat getWidth() const;
    void setHeight(NomadFloat height);
    [[nodiscard]]
    NomadFloat getHeight() const;
    [[nodiscard]]
    PointF getSize() const;

    void pause();
    void unpause();
    void setPause(bool pause);
    [[nodiscard]] bool isPaused() const;

    void hide();
    void show();
    void setVisible(bool visible);
    [[nodiscard]] bool isVisible() const;
    [[nodiscard]] bool isHidden() const;

    void setOpacity(NomadFloat opacity);
    [[nodiscard]] NomadFloat getOpacity() const;

    void stopMoving();
    void move(const PointF& velocity);
    void move(NomadFloat x, NomadFloat y);
    void startMovingInDirection(NomadFloat angle, NomadFloat speed);
    void moveTo(const PointF& destination, NomadFloat speed, NomadId onArriveAtDestination = NOMAD_INVALID_ID);
    void moveTo(NomadFloat x, NomadFloat y, NomadFloat speed, NomadId onArriveAtDestination = NOMAD_INVALID_ID);
    void moveTo(const PointF& destination, NomadFloat speed, std::unique_ptr<Closure> onArriveAtDestination);
    void moveTo(NomadFloat x, NomadFloat y, NomadFloat speed, std::unique_ptr<Closure> onArriveAtDestination);
    [[nodiscard]]
    bool isMoving() const;

    void setVelocity(NomadFloat x, NomadFloat y);
    void setVelocity(const PointF& velocity);
    void setVelocity(Cardinal direction, NomadFloat speed);
    void setVelocityX(NomadFloat x);
    void setVelocityY(NomadFloat y);
    [[nodiscard]] const PointF& getVelocity() const;

    void setDestination(NomadFloat x, NomadFloat y);
    void setDestination(const PointF& destination);
    [[nodiscard]] const PointF& getDestination() const;

    void setDestinationX(NomadFloat x);
    [[nodiscard]] NomadFloat get_destination_x() const;

    void set_destination_y(NomadFloat y);
    [[nodiscard]] NomadFloat getDestinationY() const;

    void setSpeed(NomadFloat speed);
    [[nodiscard]] NomadFloat getSpeed() const;

    void setMask(NomadInteger mask);
    [[nodiscard]] NomadInteger getMask() const;

    void setCollisionMask(NomadInteger collisionMask);
    [[nodiscard]] NomadInteger getCollisionMask() const;

    void setSensorMask(NomadInteger sensorMask);
    [[nodiscard]] NomadInteger getSensorMask() const;

    void setNoBody();
    void setCircleBody(BodyType bodyType, NomadFloat radius);
    void setRectangleBody(BodyType bodyType, NomadFloat width, NomadFloat height);
    void destroyBody();

    [[nodiscard]] BodyType getBodyType() const;
    [[nodiscard]] BodyShape getBodyShape() const;
    [[nodiscard]] NomadFloat getBodyWidth() const;
    [[nodiscard]] NomadFloat getBodyHeight() const;
    [[nodiscard]] NomadFloat getBodyRadius() const;

    [[nodiscard]] bool isTouching(const RectangleF& rectangle) const;
    [[nodiscard]] bool isTouching(const CircleF& circle) const;
    [[nodiscard]] bool isTouching(const Entity* entity) const;

    RectangleF& getBoundingBox(RectangleF& boundingBox) const;

    // System
    void addSystemBeforeUpdate(NomadString systemId, NomadId functionId);
    void addSystemAfterUpdate(NomadString systemId, NomadId functionId);
    void removeSystem(const NomadString& systemId);
    void removeAllSystems();
    void executeSystemsBeforeUpdate();
    void executeSystemsAfterUpdate();

    // Collision
    void triggerBeginCollision(Entity* other, NomadInteger mask);
    void triggerEndCollision(Entity* other, NomadInteger mask);

    // Notify the entity that it has entered the camera frame.
    void enterCamera();
    // Notify the entity that it has exited the camera frame.
    void exitCamera();

    // Function to execute when the entity enters the camera frame.
    void setOnEnterCamera(NomadId functionId);
    [[nodiscard]] NomadId getOnEnterCamera() const;

    // Function to execute when the entity exits the camera frame.
    void setOnExitCamera(NomadId functionId);
    [[nodiscard]] NomadId getOnExitCamera() const;

    [[nodiscard]] bool isInCamera() const;

    void invalidatePhysicsBody();
    void beforeSimulationUpdate(b2WorldId world);
    void afterSimulationUpdate(b2WorldId world);

    void setLayer(NomadInteger layer);
    [[nodiscard]] NomadInteger getLayer() const;

    void setSpriteName(const NomadString& spriteName);
    [[nodiscard]] const NomadString& getSpriteName() const;

    void setSprite(const Sprite* sprite);
    [[nodiscard]] const Sprite* getSprite() const;

    void setSpriteX(NomadFloat x);
    [[nodiscard]] NomadFloat getSpriteX() const;

    void setSpriteY(NomadFloat y);
    [[nodiscard]] NomadFloat getSpriteY() const;

    void setSpriteAnchor(const PointF& anchor);
    void setSpriteAnchor(NomadFloat x, NomadFloat y);
    [[nodiscard]] const PointF& getSpriteAnchor() const;

    void setAnimation(const Animation* animation);
    [[nodiscard]] const Animation* getAnimation() const;

    void setAnimationName(const NomadString& animationName);
    [[nodiscard]] const NomadString& getAnimationName() const;

    void setAnimationVariant(const NomadString& animationVariant);
    [[nodiscard]] const NomadString& getAnimationVariant() const;

    void setAnimationDirection(const NomadString& animationDirection);
    [[nodiscard]] const NomadString& getAnimationDirection() const;

    void setAnimationDuration(NomadInteger speed);
    [[nodiscard]] NomadInteger getAnimationDuration() const;

    void setAnimationRepeat(bool repeat);
    [[nodiscard]] bool getAnimationRepeat() const;

    void setAnimationReverse(bool reverse);
    [[nodiscard]] bool getAnimationReverse() const;

    void setOnAnimationEnd(NomadId functionId);
    void setOnAnimationEnd(std::unique_ptr<Closure> closure);
    [[nodiscard]] NomadId getOnAnimationEnd() const;

    void playAnimation(
        const NomadString& variant,
        NomadInteger duration,
        NomadBoolean repeat,
        NomadBoolean reverse,
        NomadId onAnimationEnd = NOMAD_INVALID_ID
    );
    void playAnimation(
        const NomadString& variant,
        NomadInteger duration,
        NomadBoolean repeat,
        NomadBoolean reverse,
        std::unique_ptr<Closure> onAnimationEnd
    );

    void setText(const NomadString& text);
    [[nodiscard]] const NomadString& getText();

    void setTextAlignment(Alignment alignment);
    [[nodiscard]] Alignment getTextAlignment() const;

    void setTextPosition(NomadFloat x, NomadFloat y);
    void setTextPosition(const PointF& position);
    [[nodiscard]] const PointF& getTextPosition() const;

    void setTextX(NomadFloat x);
    [[nodiscard]] NomadFloat getTextX() const;

    void setTextY(NomadFloat y);
    [[nodiscard]] NomadFloat getTextY() const;

    void setTextWidth(NomadFloat width);
    [[nodiscard]] NomadFloat getTextWidth() const;

    void setTextHeight(NomadFloat height);
    [[nodiscard]] NomadFloat getTextHeight() const;

    void setTextLineSpacing(NomadFloat lineSpacing);
    [[nodiscard]] NomadFloat getTextLineSpacing() const;

    void setTextColor(const Color& color);
    [[nodiscard]] Color getTextColor() const;

    void setFontById(NomadId fontId);
    [[nodiscard]] NomadId getFontId() const;

    void setVariableValue(NomadId variableId, const RuntimeValue& value);
    void getVariableValue(NomadId variableId, RuntimeValue& value) const;

    void registerUserEvent(const NomadString& name, NomadId functionId);
    void unregisterUserEvent(const NomadString& name);
    void triggerUserEvent(const NomadString& name);
    void registerEvent(NomadId eventId, std::unique_ptr<Closure> closure);
    void unregisterEvent(NomadId eventId);
    void dispatchEvent(const EventDispatch& dispatch);

    [[nodiscard]] NomadId getOnUpdate() const;
    void setOnUpdate(NomadId functionId);

    void setOnBeginCollision(NomadId functionId);
    [[nodiscard]] NomadId getOnBeginCollision() const;

    void setOnBeginCollisionWith(NomadInteger mask, NomadId functionId);
    [[nodiscard]] NomadId getOnBeginCollisionWith(NomadInteger mask) const;

    void setOnEndCollision(NomadId functionId);
    [[nodiscard]] NomadId getOnEndCollision() const;

    void setOnEndCollisionWith(NomadInteger mask, NomadId functionId);
    [[nodiscard]] NomadId getOnEndCollisionWith(NomadInteger mask) const;

private: // Functions
    void invalidateTextTexture();
    void generateTextTexture(Canvas* canvas);
    void generateBody(b2WorldId);
    // Helper to create shape.
    b2ShapeId createShape(bool sensor);

private: // Data members
    NomadId m_id = NOMAD_INVALID_ID;
    NomadString m_name;
    Scene* m_scene;
    NomadString m_functionName;

    // World position
    PointF m_position;
    PointF m_previousPosition;
    PointF m_size;
    NomadFloat m_z = 0.0;
    NomadInteger m_layer = 0;

    // State
    NomadBoolean m_paused = false;
    NomadBoolean m_visible = true;
    NomadFloat m_opacity = 1.0;

    // Movement
    PointF m_velocity = {};
    bool m_moveToDestination = false;
    PointF m_destination = {};
    NomadFloat m_speed = 0;
    NomadId m_onArriveAtDestination = NOMAD_INVALID_ID;
    std::unique_ptr<Closure> m_onArriveAtDestinationClosure;

    // Mask, body and collision
    BodyShape m_bodyShape = BodyShape::None;
    BodyType m_bodyType = BodyType::Static;
    NomadBoolean m_sensor = false;
    NomadFloat m_bodyWidth = 0;
    NomadFloat m_bodyHeight = 0;
    NomadFloat m_bodyRadius = 0;
    NomadInteger m_mask = 0;
    NomadInteger m_collisionMask = 0;
    NomadInteger m_sensorMask = 0;
    NomadBoolean m_hasBody = false;
    NomadBoolean m_hasSensorBody = false;
    NomadBoolean m_bodyInvalidated = true;
    NomadBoolean m_positionInvalidated = true;
    NomadBoolean m_velocityInvalidated = true;

    // Box2D body & shape
    b2BodyId m_b2Body = {};
    b2ShapeId m_b2Shape = {};
    b2ShapeId m_b2SensorShape = {};

    // Camera
    bool m_inCamera = false;

    // Visuals
    PointF m_spriteAnchor;  // Sprite image anchor point
    const Sprite* m_sprite = nullptr;
    const Animation* m_animation = nullptr;
    NomadInteger m_currentFrame = 0;
    NomadInteger m_frameCount = 0;

    // Animation
    NomadString m_animationName;
    NomadString m_animationVariant = "idle";
    NomadString m_animationDirection = "south";
    NomadInteger m_animationDuration = 1;
    NomadBoolean m_animationRepeat = true;
    NomadBoolean m_animationReverse = false;
    NomadId m_onAnimationEnd = NOMAD_INVALID_ID;
    std::unique_ptr<Closure> m_onAnimationEndClosure;
    bool m_animationDirty = false;

    // Text
    NomadString m_textValue;
    NomadString m_wrappedText;
    Alignment m_textAlignment = Alignment::CenterMiddle;
    PointF m_textPosition;
    NomadFloat m_textWidth = 0;
    NomadFloat m_textHeight = 0;
    NomadFloat m_textLineSpacing = 0;
    NomadId m_textFontId = NOMAD_INVALID_ID;
    Color m_textColor = Colors::Black;
    Texture* m_textTexture = nullptr;

    // Variable / Function
    VariableList m_variables;
    EventRegistrationManager m_oldEvents;

    struct NewEventRegistration {
        NomadId eventId;
        std::unique_ptr<Closure> closure;
    };
    std::vector<NewEventRegistration> m_eventRegistrations;

    // Systems
    struct SystemRegistration {
        NomadString systemId;
        NomadId functionId;
    };
    std::vector<SystemRegistration> m_systemsBeforeUpdate;
    std::vector<SystemRegistration> m_systemsAfterUpdate;
};

using EntityList = std::vector<Entity*>;

} // nomad
