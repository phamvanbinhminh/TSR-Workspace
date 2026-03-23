#pragma once
#include "../Object/Object.h"
#include "../Components/TransformComponent.h"
#include "../Components/SpriteComponent.h"
#include <vector>

// RenderSystem: lấy Transform + Sprite rồi gọi RenderAt()
class RenderSystem
{
public:
    void Render(const std::vector<Object*>& objects)
    {
        for (auto* obj : objects)
        {
            if (!obj || !obj->IsActive()) continue;

            auto* tf  = obj->GetComponent<TransformComponent>();
            auto* spr = obj->GetComponent<SpriteComponent>();
            if (!tf || !spr) continue;

            spr->RenderAt(tf->x, tf->y,
                          tf->scaleX, tf->scaleY,
                          tf->flipX,  tf->flipY);
        }
    }
};
