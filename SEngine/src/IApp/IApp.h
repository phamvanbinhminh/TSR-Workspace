#pragma once
#include <string>

class Renderer;

#include "Renderer/Renderer.h"
#include "../SExportEngineAPI.h"
class  Application
{
protected:
    int _width,_height;
    std::string _title;
	Renderer* _renderer; // con trỏ đến renderer, renderer sẽ được tạo khi nạp vào engine

public:
    int getWindowWidth() const {return _width;};
    int getWindowHeight() const {return _height;};
    std::string getWindowTitle() const {return _title;};

	void setRenderer(Renderer* r) { _renderer = r; };
public:
    virtual ~Application() {}
    Application(int width, int height, const std::string& title)
        : _width(width), _height(height), _title(title) {
    }

    virtual void Init() {}
    virtual void Update(float dt) {}
    virtual void Render() {}
};