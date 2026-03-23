#pragma once
#include "Window.h"
#include "Renderer/Renderer.h"
#include "IApp/IApp.h"

#include "../SExportEngineAPI.h"

class SENGINE_API Engine
{
public:
    Engine(Application* game);
    ~Engine();

    void Run();

private:
    Window _window;
    Renderer _renderer;
    Application* _game;
};