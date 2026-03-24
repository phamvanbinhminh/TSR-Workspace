#pragma once

#include <string>
#include "../SExportEngineAPI.h"


#include <ft2build.h>
#include FT_FREETYPE_H
#include <map>

struct Character
{
    unsigned int texture;

    int width;
    int height;

    int bearingX;
    int bearingY;

    unsigned int advance;
};

class TextRenderer
{
public:
    TextRenderer() : ft(nullptr), face(nullptr) {}
    ~TextRenderer();

    bool Init(const std::string& fontFile, int size);

    void DrawText2D(
        const std::string& text,
        float x,
        float y,
        float scale
    );

    // Load glyph on-demand theo Unicode codepoint
    bool LoadGlyph(unsigned long codepoint);

private:

    std::map<unsigned long, Character> chars;

    FT_Library ft;
    FT_Face face;
};
class SENGINE_API Renderer
{
private:
	int _screenWidth = 800;
	int _screenHeight = 600;

    // Camera offset (world → screen)
    float _camX = 0.f;
    float _camY = 0.f;
    bool  _camActive = false;  // true khi đang trong BeginWorldDraw

public:
	Renderer();
    Renderer(int screenWith, int screenHeight);
    ~Renderer();
    Renderer& operator=(const Renderer&);

    bool Init();

    void Clear();

    void Present();

    void DrawTest();

    // ── Camera ─────────────────────────────────────────────
    // Đặt camera world position (góc trên-trái của viewport trong world)
    void SetCamera(float camX, float camY) { _camX = camX; _camY = camY; }
    float GetCameraX() const { return _camX; }
    float GetCameraY() const { return _camY; }

    // Bắt đầu vẽ world (apply camera transform)
    // Tất cả DrawTexture/DrawRect trong BeginWorldDraw..EndWorldDraw sẽ bị dịch chuyển
    void BeginWorldDraw();
    // Kết thúc world draw, trở về screen space (cho HUD)
    void EndWorldDraw();

    unsigned int LoadTexture(const std::string& file);
    unsigned int LoadTextureFromMemory(unsigned char* data, int length);
    uint32_t LoadTextureRawRGBA(unsigned char* data,
                           int width, int height);

    // DrawTexture/DrawRect dùng world coords khi ở giữa BeginWorldDraw/EndWorldDraw
    void DrawTexture(unsigned int tex, float x, float y, float w, float h);

    // Vẽ hình chữ nhật màu phẳng (r,g,b,a: 0.0~1.0)
    void DrawRect(float x, float y, float w, float h,
                  float r, float g, float b, float a = 1.0f);

    // Vẽ hình chữ nhật với outline + đường chéo //// bên trong (debug overlay)
    // spacing: khoảng cách giữa các đường chéo (pixels)
    void DrawHatchRect(float x, float y, float w, float h,
                       float r, float g, float b, float a = 0.6f,
                       float spacing = 8.f);

#undef DrawText
    void DrawText(
        const std::string& text,
        float x,
        float y,
        float scale
    );

    int GetScreenWidth()  const { return _screenWidth;  }
    int GetScreenHeight() const { return _screenHeight; }

private:

    TextRenderer _text;
};

