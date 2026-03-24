#include "Core/Engine.h"
#include "IApp/IApp.h"
#include "../MapEdit/MapEditor.h"
#include "../PakEdit/PakEditor.h"
#include "../SprEdit/SprEditor.h"
#include "AppConfig.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

class MyApp : public Application
{
public:

    MyApp(int width, int height, const std::string& title)
        : Application(width, height, title) {
    }

    ~MyApp()
    {
        // ── Lưu config trước khi tắt ──────────────────────────
        _mapEditor.SaveToConfig();
        AppConfig::Get().showMapEditor = _showMapEditor;
        AppConfig::Get().showSprEditor = _showSprEditor;
        AppConfig::Get().showPakEditor = _showPakEditor;
        AppConfig::Get().Save();

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void Init() override
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGui::StyleColorsDark();

        GLFWwindow* window = glfwGetCurrentContext();

        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 130");

        // ── Load config ngay sau khi ImGui khởi động xong ─────
        AppConfig& cfg = AppConfig::Get();
        cfg.Load();

        // Restore window-visibility state
        _showMapEditor = cfg.showMapEditor;
        _showSprEditor = cfg.showSprEditor;
        _showPakEditor = cfg.showPakEditor;

        // Restore MapEditor state + auto-load last map
        _mapEditor.LoadFromConfig();
    }

    void Update(float dt) override
    {
        // Update editors if needed
    }

    void Render() override
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawMenuBar();
        DrawMainEditor();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

private:

    void DrawMenuBar()
    {
        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                ImGui::MenuItem("New Project");
                ImGui::MenuItem("Open Project");
                ImGui::MenuItem("Save");

                ImGui::Separator();

                if (ImGui::MenuItem("Exit"))
                {
                    // Lưu config trước khi exit
                    _mapEditor.SaveToConfig();
                    AppConfig::Get().showMapEditor = _showMapEditor;
                    AppConfig::Get().showSprEditor = _showSprEditor;
                    AppConfig::Get().showPakEditor = _showPakEditor;
                    AppConfig::Get().Save();
                    exit(0);
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Editors"))
            {
                if (ImGui::MenuItem("Map Editor"))
                    _showMapEditor = true;

                if (ImGui::MenuItem("PAK Editor"))
                    _showPakEditor = true;

                if (ImGui::MenuItem("SPR Editor"))
                    _showSprEditor = true;

                ImGui::EndMenu();
            }

            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

            ImGui::EndMainMenuBar();
        }
    }

    template<typename T>
    void DrawEditorWindow(bool& open, T& editor)
    {
        if (!open) return;
        editor.RenderEditor();
    }

    void DrawMainEditor()
    {
        DrawEditorWindow(_showMapEditor, _mapEditor);
        DrawEditorWindow(_showPakEditor, _pakEditor);
        DrawEditorWindow(_showSprEditor, _sprEditor);
    }

    enum class EditorType
    {
        Map,
        Pak,
        Spr
    };

    EditorType _currentEditor = EditorType::Map;
    MapEditor _mapEditor;
    PakEditor _pakEditor;
    SprEditor _sprEditor;

    bool _showMapEditor = false;
    bool _showPakEditor = false;
    bool _showSprEditor = false;
};

int main()
{
    MyApp app(1280, 720, "ResManager Tool");
    Engine engine(&app);
    engine.Run();

    return 0;
}
