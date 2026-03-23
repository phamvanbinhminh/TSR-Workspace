#include "ScriptComponent.h"
#include "../Object/Object.h"
#include "../Components/TransformComponent.h"
#include "../Components/SpriteComponent.h"
#include "../../ResManager/VFS/VFS.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <cstring>

// ── Lua C API thay mặt TransformComponent ────────────────────────────────────

// entity_setVelocity(vx, vy)
static int lua_setVelocity(lua_State* L)
{
    // lấy Object* từ upvalue
    Object* obj = reinterpret_cast<Object*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    float vx = (float)luaL_checknumber(L, 1);
    float vy = (float)luaL_checknumber(L, 2);

    auto* tf = obj->GetComponent<TransformComponent>();
    if (tf) tf->SetVelocity(vx, vy);
    return 0;
}

// entity_getPosition() → x, y
static int lua_getPosition(lua_State* L)
{
    Object* obj = reinterpret_cast<Object*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    auto* tf = obj->GetComponent<TransformComponent>();
    if (tf)
    {
        lua_pushnumber(L, tf->x);
        lua_pushnumber(L, tf->y);
        return 2;
    }
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
}

// entity_playAnimation(name)
static int lua_playAnimation(lua_State* L)
{
    Object* obj = reinterpret_cast<Object*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    const char* name = luaL_checkstring(L, 1);
    auto* spr = obj->GetComponent<SpriteComponent>();
    if (spr) spr->PlayAnimation(name);
    return 0;
}

// entity_flipX(bool)
static int lua_setFlipX(lua_State* L)
{
    Object* obj = reinterpret_cast<Object*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    bool flip = lua_toboolean(L, 1) != 0;
    auto* tf = obj->GetComponent<TransformComponent>();
    if (tf) tf->flipX = flip;
    return 0;
}

// ── Lua Input API ─────────────────────────────────────────────────────────────

// Input.isKeyDown(key)  key = GLFW_KEY_* constant (integer)
static int lua_isKeyDown(lua_State* L)
{
    GLFWwindow* win = reinterpret_cast<GLFWwindow*>(
        lua_touserdata(L, lua_upvalueindex(1)));
    int key = (int)luaL_checkinteger(L, 1);
    bool down = (glfwGetKey(win, key) == GLFW_PRESS);
    lua_pushboolean(L, down ? 1 : 0);
    return 1;
}

// ── ScriptComponent ───────────────────────────────────────────────────────────

ScriptComponent::ScriptComponent(Object* owner,
                                 const std::string& scriptPath,
                                 GLFWwindow* window)
    : Component(owner)
    , _scriptPath(scriptPath)
    , _window(window)
{
    _L = luaL_newstate();
    luaL_openlibs(_L);
}

ScriptComponent::~ScriptComponent()
{
    if (_L)
    {
        lua_close(_L);
        _L = nullptr;
    }
}

void ScriptComponent::RegisterAPI()
{
    // ── Entity table ─────────────────────────────────────────────────────
    lua_newtable(_L);                    // push table "Entity"

    // push upvalue = Object* cho mỗi function
    lua_pushlightuserdata(_L, _owner);
    lua_pushcclosure(_L, lua_setVelocity, 1);
    lua_setfield(_L, -2, "setVelocity");

    lua_pushlightuserdata(_L, _owner);
    lua_pushcclosure(_L, lua_getPosition, 1);
    lua_setfield(_L, -2, "getPosition");

    lua_pushlightuserdata(_L, _owner);
    lua_pushcclosure(_L, lua_playAnimation, 1);
    lua_setfield(_L, -2, "playAnimation");

    lua_pushlightuserdata(_L, _owner);
    lua_pushcclosure(_L, lua_setFlipX, 1);
    lua_setfield(_L, -2, "setFlipX");

    lua_setglobal(_L, "Entity");

    // ── Input table ──────────────────────────────────────────────────────
    lua_newtable(_L);

    lua_pushlightuserdata(_L, _window);
    lua_pushcclosure(_L, lua_isKeyDown, 1);
    lua_setfield(_L, -2, "isKeyDown");

    // Expose GLFW key constants
    lua_pushinteger(_L, GLFW_KEY_W);     lua_setfield(_L, -2, "KEY_W");
    lua_pushinteger(_L, GLFW_KEY_A);     lua_setfield(_L, -2, "KEY_A");
    lua_pushinteger(_L, GLFW_KEY_S);     lua_setfield(_L, -2, "KEY_S");
    lua_pushinteger(_L, GLFW_KEY_D);     lua_setfield(_L, -2, "KEY_D");
    lua_pushinteger(_L, GLFW_KEY_UP);    lua_setfield(_L, -2, "KEY_UP");
    lua_pushinteger(_L, GLFW_KEY_DOWN);  lua_setfield(_L, -2, "KEY_DOWN");
    lua_pushinteger(_L, GLFW_KEY_LEFT);  lua_setfield(_L, -2, "KEY_LEFT");
    lua_pushinteger(_L, GLFW_KEY_RIGHT); lua_setfield(_L, -2, "KEY_RIGHT");
    lua_pushinteger(_L, GLFW_KEY_SPACE); lua_setfield(_L, -2, "KEY_SPACE");

    lua_setglobal(_L, "Input");
}

bool ScriptComponent::Load()
{
    if (!_L) return false;
    RegisterAPI();

    if (luaL_dofile(_L, _scriptPath.c_str()) != LUA_OK)
    {
        std::cerr << "[ScriptComponent] Load error: "
                  << lua_tostring(_L, -1) << "\n";
        lua_pop(_L, 1);
        return false;
    }

    // Gọi Init() nếu có
    lua_getglobal(_L, "Init");
    if (lua_isfunction(_L, -1))
    {
        if (lua_pcall(_L, 0, 0, 0) != LUA_OK)
        {
            std::cerr << "[ScriptComponent] Init() error: "
                      << lua_tostring(_L, -1) << "\n";
            lua_pop(_L, 1);
        }
    }
    else
    {
        lua_pop(_L, 1);
    }

    _loaded = true;
    return true;
}

bool ScriptComponent::CallUpdate(float dt)
{
    if (!_loaded || !_L) return false;

    lua_getglobal(_L, "Update");
    if (!lua_isfunction(_L, -1))
    {
        lua_pop(_L, 1);
        return true;
    }

    lua_pushnumber(_L, dt);
    if (lua_pcall(_L, 1, 0, 0) != LUA_OK)
    {
        std::cerr << "[ScriptComponent] Update() error: "
                  << lua_tostring(_L, -1) << "\n";
        lua_pop(_L, 1);
        return false;
    }
    return true;
}

// ── LoadFromBuffer ────────────────────────────────────────────────────────────

bool ScriptComponent::LoadFromBuffer(const std::vector<uint8_t>& code,
                                     const std::string& debugName)
{
    if (!_L || code.empty()) return false;
    RegisterAPI();

    const char* chunkName = debugName.empty() ? _scriptPath.c_str() : debugName.c_str();

    // luaL_loadbuffer: compile Lua từ memory
    int status = luaL_loadbuffer(_L,
        reinterpret_cast<const char*>(code.data()),
        code.size(),
        chunkName);

    if (status != LUA_OK)
    {
        std::cerr << "[ScriptComponent] LoadFromBuffer compile error: "
                  << lua_tostring(_L, -1) << "\n";
        lua_pop(_L, 1);
        return false;
    }

    // Execute script
    if (lua_pcall(_L, 0, 0, 0) != LUA_OK)
    {
        std::cerr << "[ScriptComponent] LoadFromBuffer exec error: "
                  << lua_tostring(_L, -1) << "\n";
        lua_pop(_L, 1);
        return false;
    }

    // Gọi Init() nếu có
    lua_getglobal(_L, "Init");
    if (lua_isfunction(_L, -1))
    {
        if (lua_pcall(_L, 0, 0, 0) != LUA_OK)
        {
            std::cerr << "[ScriptComponent] Init() error: "
                      << lua_tostring(_L, -1) << "\n";
            lua_pop(_L, 1);
        }
    }
    else
    {
        lua_pop(_L, 1);
    }

    _loaded = true;
    return true;
}

// ── LoadFromVFS ───────────────────────────────────────────────────────────────

bool ScriptComponent::LoadFromVFS(const std::string& virtualPath)
{
    auto bytes = VFS::Get().ReadFile(virtualPath);
    if (bytes.empty())
    {
        std::cerr << "[ScriptComponent] VFS not found: " << virtualPath << "\n";
        return false;
    }
    return LoadFromBuffer(bytes, virtualPath);
}
