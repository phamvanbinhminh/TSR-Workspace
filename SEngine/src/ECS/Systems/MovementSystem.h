#pragma once
#include "../Object/Object.h"
#include "../Components/TransformComponent.h"
#include <vector>

// MovementSystem: áp velocity vào position mỗi frame
class MovementSystem
{
public:
    void Update(float dt, const std::vector<Object*>& objects)
    {
        for (auto* obj : objects)
        {
            if (!obj || !obj->IsActive()) continue;
            auto* tf = obj->GetComponent<TransformComponent>();
            if (!tf) continue;
            tf->x += tf->vx * dt;
            tf->y += tf->vy * dt;
        }
    }
};
