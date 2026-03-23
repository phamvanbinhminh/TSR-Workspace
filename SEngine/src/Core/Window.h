
#pragma once
#include <string>

struct GLFWwindow;


#include <GLFW/glfw3.h>

#include "../SExportEngineAPI.h"
class Window
{
public:
    bool Init(int width,int height,const std::string& title);
    bool ShouldClose();
    void PollEvents();
	void SwapBuffers() { glfwSwapBuffers(_window); }

private:
    GLFWwindow* _window = nullptr;
};
