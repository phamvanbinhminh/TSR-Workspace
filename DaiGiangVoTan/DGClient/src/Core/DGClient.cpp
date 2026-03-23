#include "Core/Engine.h"
#include "IApp/IApp.h"
#include "Scene/SceneManager.h"

#include "../VFS/ClientVFS.h"
#include "../Scene/LoginScene.h"

#include <GLFW/glfw3.h>
#include <iostream>

// ─────────────────────────────────────────────────────────────────────────────
// DGClientApp — entry point cho game DaiGiangVoTan (client)
//
// Flow:
//   1. Init()   → ClientVFS::Init()  (mount res/maps/script pak)
//              → SceneManager push LoginScene
//   2. Update() → SceneManager::Update(dt)
//                  LoginScene tự Replace → GameScene khi login xong
//   3. Render() → SceneManager::Render()
// ─────────────────────────────────────────────────────────────────────────────
class DGClientApp : public Application
{
public:
    DGClientApp(int width, int height, const std::string& title)
        : Application(width, height, title)
    {
    }

    void Init() override
    {
        // 1. Mount các pak tài nguyên của client
        ClientVFS::Init();   // dùng thư mục hiện tại (cạnh exe)

        // 2. Đẩy LoginScene vào SceneManager
        GLFWwindow* window = glfwGetCurrentContext();
        SceneManager::Get().Push(
            std::make_unique<LoginScene>(window, _renderer, _width, _height));

        std::cout << "[DGClient] Init xong. Đang ở LoginScene.\n";
    }

    void Update(float dt) override
    {
        SceneManager::Get().Update(dt);
    }

    void Render() override
    {
        SceneManager::Get().Render();
    }

    ~DGClientApp() override
    {
        ClientVFS::Shutdown();
    }
};

int main()
{
    DGClientApp game(1280, 720, "Dai Giang Vo Tan");
    Engine engine(&game);
    engine.Run();
    return 0;
}
