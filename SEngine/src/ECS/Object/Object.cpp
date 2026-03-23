#include "Object.h"

int Object::_nextID = 0;

Object::Object()
    : _id(++_nextID), _active(true)
{
}

Object::~Object()
{
    _components.clear();
    _componentMap.clear();
}

void Object::Update(float deltaTime)
{
    if (!_active) return;
    for (auto& comp : _components)
        comp->Update(deltaTime);
}

void Object::Render()
{
    if (!_active) return;
    for (auto& comp : _components)
        comp->Render();
}
