#include "Engine.h"

Engine::Engine(Application* g)
{
    _game = g;
	_renderer = Renderer(_game->getWindowWidth(), _game->getWindowHeight());
	_game->setRenderer(&_renderer);
}

Engine::~Engine()
{
}

#include <GLFW/glfw3.h>
void Engine::Run()
{
    _window.Init(
        _game->getWindowWidth(),
        _game->getWindowHeight(),
        _game->getWindowTitle()
    );

	_renderer.Init();
    _game->Init();

    float lastTime = glfwGetTime();

    while(!_window.ShouldClose())
    {
        float current = glfwGetTime();
        float dt = current-lastTime;
        lastTime=current;
        _window.PollEvents();
        _game->Update(dt);
        _renderer.Clear();
        _game->Render();
		_window.SwapBuffers();
    }
}