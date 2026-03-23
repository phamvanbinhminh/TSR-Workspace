#pragma once
#include "../Object/Object.h"
#include "../Components/ScriptComponent.h"
#include <vector>

// ScriptSystem: gọi Lua Update(dt) cho mỗi entity có ScriptComponent
class ScriptSystem
{
public:
    void Update(float dt, const std::vector<Object*>& objects)
    {
        for (auto* obj : objects)
        {
            if (!obj || !obj->IsActive()) continue;
            auto* sc = obj->GetComponent<ScriptComponent>();
            if (!sc || !sc->IsLoaded()) continue;
            sc->CallUpdate(dt);
        }
    }
};
