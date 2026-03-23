#include "Scene.h"
#include <algorithm>

Scene::Scene() {}

Scene::~Scene()
{
    _objects.clear();
    _objectPtrs.clear();
}

void Scene::RebuildPtrCache()
{
    _objectPtrs.clear();
    _objectPtrs.reserve(_objects.size());
    for (auto& o : _objects)
        _objectPtrs.push_back(o.get());
}

Object* Scene::CreateObject(const std::string& name)
{
    auto obj = std::make_unique<Object>();
    if (!name.empty())
        obj->SetName(name);
    Object* ptr = obj.get();
    _objects.push_back(std::move(obj));
    _objectPtrs.push_back(ptr);
    return ptr;
}

void Scene::DestroyObject(Object* obj)
{
    auto it = std::find_if(_objects.begin(), _objects.end(),
        [obj](const std::unique_ptr<Object>& o) { return o.get() == obj; });
    if (it != _objects.end())
    {
        _objects.erase(it);
        RebuildPtrCache();
    }
}

void Scene::Update(float dt)
{
    // Thứ tự: Script → Movement → Physics
    _script  .Update(dt, _objectPtrs);
    _movement.Update(dt, _objectPtrs);
    _physics .Update(dt, _objectPtrs);

    // Sau đó update component-level (SpriteComponent advance frame)
    for (auto* obj : _objectPtrs)
        if (obj && obj->IsActive())
            obj->Update(dt);
}

Object* Scene::FindObject(const std::string& name)
{
    for (auto& o : _objects)
        if (o && o->GetName() == name)
            return o.get();
    return nullptr;
}

const Object* Scene::FindObject(const std::string& name) const
{
    for (const auto& o : _objects)
        if (o && o->GetName() == name)
            return o.get();
    return nullptr;
}

void Scene::Render()
{
    _render.Render(_objectPtrs);
}
