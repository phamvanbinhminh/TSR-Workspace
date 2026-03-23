#pragma once
#include "../Component.h"

// TransformComponent: vị trí 2D, velocity, rotation, scale, flip
class TransformComponent : public Component
{
public:
    TransformComponent(Object* owner,
                       float x = 0.f, float y = 0.f,
                       float scaleX = 1.f, float scaleY = 1.f)
        : Component(owner)
        , x(x), y(y)
        , vx(0.f), vy(0.f)
        , rotation(0.f)
        , scaleX(scaleX), scaleY(scaleY)
        , flipX(false), flipY(false)
    {}

    // Position
    float x = 0.f;
    float y = 0.f;

    // Velocity (set bởi Lua ScriptSystem, apply bởi MovementSystem)
    float vx = 0.f;
    float vy = 0.f;

    // Rotation (degrees)
    float rotation = 0.f;

    // Scale
    float scaleX = 1.f;
    float scaleY = 1.f;

    // Flip
    bool flipX = false;
    bool flipY = false;

    // Helpers
    void SetPosition(float px, float py) { x = px; y = py; }
    void Translate(float dx, float dy)   { x += dx; y += dy; }
    void SetVelocity(float pvx, float pvy) { vx = pvx; vy = pvy; }
    void StopVelocity() { vx = 0.f; vy = 0.f; }

    void Update(float /*dt*/) override {}  // physics/movement xử lý ngoài
};
