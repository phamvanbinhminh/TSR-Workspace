#include "Core/Engine.h"
#include "IApp/IApp.h"
#include "Renderer/Renderer.h"
#include <iostream>
#include <sstream>


#include <vector>
#include <cstdlib>
#include <cmath>

struct Sprite
{
    float x, y;
    float vx, vy;
};

class MyGame : public Application
{
public:
    MyGame(int width, int height, const std::string& title)
        : Application(width, height, title)
    {
    }

private:

    unsigned int tex = 0;

    float w = 32;
    float h = 32;

    std::vector<Sprite> sprites;

    float fps = 0;
    float fpsTimer = 0;
    int frameCount = 0;

public:

    void Init() override
    {
        tex = _renderer->LoadTexture("test.png");

        int count = 1000;
        sprites.resize(count);

        for (auto& s : sprites)
        {
            s.x = rand() % _width;
            s.y = rand() % _height;

            float angle = (rand() % 360) * 3.14159f / 180.0f;
            float speed = 200;

            s.vx = cos(angle) * speed;
            s.vy = sin(angle) * speed;
        }
    }

    void Update(float dt) override
    {
        frameCount++;
        fpsTimer += dt;

        if (fpsTimer >= 1.0f)
        {
            fps = frameCount / fpsTimer;
            frameCount = 0;
            fpsTimer = 0;
        }

        for (auto& s : sprites)
        {
            s.x += s.vx * dt;
            s.y += s.vy * dt;

            if (s.x < 0)
            {
                s.x = 0;
                s.vx = -s.vx;
            }

            if (s.x + w > _width)
            {
                s.x = _width - w;
                s.vx = -s.vx;
            }

            if (s.y < 0)
            {
                s.y = 0;
                s.vy = -s.vy;
            }

            if (s.y + h > _height)
            {
                s.y = _height - h;
                s.vy = -s.vy;
            }
        }
    }

    void Render() override
    {
        for (auto& s : sprites)
        {
            _renderer->DrawTexture(tex, s.x, s.y, w, h);
        }

        std::stringstream ss;
        ss << "FPS: " << (int)fps;

        _renderer->DrawText(ss.str(), 10, 10, 1);
    }
};

#include "ResManager/Pak/Pak.h"
#include "ResManager/Spr/Spr.h"
int main()
{
    MyGame game(1280, 720, "My Engine");
    Engine engine(&game);

    engine.Run();


 //   Pak pak;
 //   pak.Load("game.pak");
 //   auto& data = pak.Get("map1.wor");
	//freopen("map1.wor", "wb", stdout);
	//fwrite(data.data(), 1, data.size(), stdout);
	

    //Pak::Create("assets", "game.pak");

    return 0;
}