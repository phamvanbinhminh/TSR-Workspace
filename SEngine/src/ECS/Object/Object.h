#pragma once
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <typeinfo>
#include <typeindex>

#include "../Component.h"
#include "../../SExportEngineAPI.h"

class SENGINE_API Object
{
public:
    Object();
    virtual ~Object();

    // Non-copyable, movable
    Object(const Object&)            = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&)                 = default;
    Object& operator=(Object&&)      = default;

    // Identification
    const std::string& GetName() const { return _name; }
    void SetName(const std::string& name) { _name = name; }
    int GetID() const { return _id; }

    // Active flag
    bool IsActive() const { return _active; }
    void SetActive(bool active) { _active = active; }

    // Component management
    template<typename T, typename... Args>
    T* AddComponent(Args&&... args)
    {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        auto comp = std::make_unique<T>(this, std::forward<Args>(args)...);
        T* ptr = comp.get();
        _componentMap[std::type_index(typeid(T))] = ptr;
        _components.push_back(std::move(comp));
        return ptr;
    }

    template<typename T>
    T* GetComponent() const
    {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        auto it = _componentMap.find(std::type_index(typeid(T)));
        if (it != _componentMap.end())
            return static_cast<T*>(it->second);
        return nullptr;
    }

    template<typename T>
    bool HasComponent() const
    {
        static_assert(std::is_base_of<Component, T>::value, "T must derive from Component");
        return _componentMap.find(std::type_index(typeid(T))) != _componentMap.end();
    }

    // Update all components
    void Update(float deltaTime);
    // Render all components
    void Render();

    const std::vector<std::unique_ptr<Component>>& GetComponents() const { return _components; }

protected:
    std::string _name;
    int         _id;
    bool        _active = true;
    static int  _nextID;

    std::vector<std::unique_ptr<Component>>        _components;
    std::unordered_map<std::type_index, Component*> _componentMap;
};
