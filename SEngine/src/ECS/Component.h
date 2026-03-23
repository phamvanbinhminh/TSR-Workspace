#pragma once
#include "../SExportEngineAPI.h"

class Object;

// Base class cho tất cả components trong ECS
class SENGINE_API Component
{
public:
    Component(Object* owner) : _owner(owner) {}
    virtual ~Component() {}

    Object* GetOwner() const { return _owner; }

    virtual void Update(float deltaTime) {}
    virtual void Render() {}

protected:
    Object* _owner;
};
