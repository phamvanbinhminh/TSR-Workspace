#pragma once
#include "../Object/Object.h"
#include "../Components/TransformComponent.h"
#include <vector>

// PhysicsSystem: stub – giới hạn entity không ra khỏi màn hình
// Mở rộng sau: AABB collision, gravity, ...
class PhysicsSystem
{
public:
    PhysicsSystem(float worldW = 800.f, float worldH = 600.f)
        : _worldW(worldW), _worldH(worldH) {}

    void SetWorldSize(float w, float h) { _worldW = w; _worldH = h; }

    void Update(float /*dt*/, const std::vector<Object*>& objects)
    {
        for (auto* obj : objects)
        {
            if (!obj || !obj->IsActive()) continue;
            auto* tf = obj->GetComponent<TransformComponent>();
            if (!tf) continue;

            // Clamp position trong world bounds (đơn giản)
            if (tf->x < 0.f) { tf->x = 0.f; tf->vx = 0.f; }
            if (tf->y < 0.f) { tf->y = 0.f; tf->vy = 0.f; }
            if (tf->x > _worldW) { tf->x = _worldW; tf->vx = 0.f; }
            if (tf->y > _worldH) { tf->y = _worldH; tf->vy = 0.f; }
        }
    }

private:
    float _worldW;
    float _worldH;
};
