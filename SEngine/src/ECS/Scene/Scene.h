#pragma once
#include "../Object/Object.h"
#include "../Systems/MovementSystem.h"
#include "../Systems/RenderSystem.h"
#include "../Systems/ScriptSystem.h"
#include "../Systems/PhysicsSystem.h"

#include <vector>
#include <memory>
#include <string>

// Scene: quản lý tập hợp Object, chạy các System theo thứ tự mỗi frame

#include "../../SExportEngineAPI.h"
class SENGINE_API Scene
{
public:
    Scene();
    virtual ~Scene();

    // Non-copyable, movable
    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&)                 = default;
    Scene& operator=(Scene&&)      = default;

    // Khởi tạo scene (override để tạo entities)
    virtual void Init() {}

    // Tạo một Object mới, Scene giữ ownership
    Object* CreateObject(const std::string& name = "");

    // Xoá object theo pointer
    void DestroyObject(Object* obj);

    // Lấy tất cả objects (raw pointer view)
    const std::vector<Object*>& GetObjects() const { return _objectPtrs; }

    // Tìm Object theo tên (trả về nullptr nếu không tìm thấy)
    Object* FindObject(const std::string& name);
    const Object* FindObject(const std::string& name) const;

    // Main loop hooks
    virtual void Update(float dt);
    virtual void Render();

    // Cho phép truy cập systems nếu cần tuỳ chỉnh
    PhysicsSystem& GetPhysicsSystem() { return _physics; }

protected:
    std::vector<std::unique_ptr<Object>> _objects;
    std::vector<Object*>                 _objectPtrs;  // cached raw ptrs

    MovementSystem _movement;
    RenderSystem   _render;
    ScriptSystem   _script;
    PhysicsSystem  _physics;

private:
    void RebuildPtrCache();
};
