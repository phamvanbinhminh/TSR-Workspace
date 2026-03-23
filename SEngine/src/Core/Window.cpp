
#include "Window.h"
#include <GLFW/glfw3.h>

bool Window::Init(int width,int height,const std::string& title)
{
    if(!glfwInit())
        return false;

    _window = glfwCreateWindow(width,height,title.c_str(),NULL,NULL);
    glfwMakeContextCurrent(_window);

    return true;
}

bool Window::ShouldClose()
{
    return glfwWindowShouldClose(_window);
}

void Window::PollEvents()
{
    glfwPollEvents();
}
