#pragma once
#include "../Component.h"
#include "../../SExportEngineAPI.h"
#include <string>
#include <vector>
#include <cstdint>

// LuaJIT
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// Forward declaration
struct GLFWwindow;

// ScriptComponent: gắn 1 file Lua vào Object.
// - Lua có thể gọi các API qua table Entity / Input
// - ScriptSystem sẽ gọi Update(dt) mỗi frame
class SENGINE_API ScriptComponent : public Component
{
public:
    ScriptComponent(Object* owner,
                    const std::string& scriptPath,
                    GLFWwindow* window);
    ~ScriptComponent();

    // Load từ file disk (gọi function Init nếu có)
    bool Load();

    // Load từ buffer bytes (từ VFS/PAK) — scriptPath chỉ dùng để debug/log
    bool LoadFromBuffer(const std::vector<uint8_t>& code,
                        const std::string& debugName = "");

    // Load từ VFS (tự động ReadFile rồi LoadFromBuffer)
    bool LoadFromVFS(const std::string& virtualPath);

    bool IsLoaded() const { return _loaded; }

    lua_State*   GetLuaState()  const { return _L; }
    GLFWwindow*  GetWindow()    const { return _window; }
    const std::string& GetScriptPath() const { return _scriptPath; }

    // ScriptSystem sẽ gọi CallUpdate thay vì Update() trực tiếp
    // để có thể bắt lỗi Lua
    bool CallUpdate(float dt);

    // Update() của Component không làm gì vì ScriptSystem xử lý
    void Update(float /*dt*/) override {}

private:
    lua_State*   _L      = nullptr;
    bool         _loaded = false;
    std::string  _scriptPath;
    GLFWwindow*  _window;

    void RegisterAPI();   // đăng ký bảng Entity / Input vào Lua state
};
