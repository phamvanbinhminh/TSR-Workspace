#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <gl/GL.h>

#include <string>
#include <sstream>

#include "Renderer.h"
#include "stb_image.h"

static GLuint g_fontBase = 0;

Renderer::Renderer()
{
}

Renderer::Renderer(int screenWidth, int screenHeight)
{
    _screenWidth = screenWidth;
    _screenHeight = screenHeight;
}

Renderer::~Renderer()
{
}

Renderer& Renderer::operator=(const Renderer& other)
{
    _screenWidth = other._screenWidth;
    _screenHeight = other._screenHeight;
    return *this;
}

bool Renderer::Init()
{
    glEnable(GL_TEXTURE_2D);

    // Bật alpha blending chuẩn: src_alpha * src + (1 - src_alpha) * dst
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, _screenWidth, _screenHeight, 0, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    _text.Init("C:/Windows/Fonts/tahoma.ttf", 48);
    return true;
}

void Renderer::Clear()
{
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::Present()
{
    glfwSwapBuffers(glfwGetCurrentContext());
}

unsigned int Renderer::LoadTexture(const std::string& file)
{
    int w,h,channels;

    //stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load(file.c_str(), &w, &h, &channels, 4);

    if(!data)
        return 0;

    unsigned int tex;

    glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);

    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        w,
        h,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        data
    );

    stbi_image_free(data);

    return tex;
}

unsigned int Renderer::LoadTextureFromMemory(unsigned char* bytes, int length)
{
    int w,h,ch;

    stbi_set_flip_vertically_on_load(true);

    unsigned char* data =
        stbi_load_from_memory(bytes,length,&w,&h,&ch,4);

    if(!data)
        return 0;

    unsigned int tex;

    glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);

    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        w,
        h,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        data
    );

    stbi_image_free(data);

    return tex;
}
uint32_t Renderer::LoadTextureRawRGBA(unsigned char* data,
                           int width, int height)
{
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    return tex;
}
void Renderer::DrawTexture(unsigned int tex,float x,float y,float w,float h)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.f, 1.f, 1.f, 1.f);   // reset color, không tint
    glBindTexture(GL_TEXTURE_2D, tex);

    glBegin(GL_QUADS);

    glTexCoord2f(0,0);
    glVertex2f(x,y);

    glTexCoord2f(1,0);
    glVertex2f(x+w,y);

    glTexCoord2f(1,1);
    glVertex2f(x+w,y+h);

    glTexCoord2f(0,1);
    glVertex2f(x,y+h);

    glEnd();
}

void Renderer::DrawRect(float x, float y, float w, float h,
                        float r, float g, float b, float a)
{
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
        glVertex2f(x,     y    );
        glVertex2f(x + w, y    );
        glVertex2f(x + w, y + h);
        glVertex2f(x,     y + h);
    glEnd();

    glColor4f(1.f, 1.f, 1.f, 1.f);   // reset
    glEnable(GL_TEXTURE_2D);
}

void Renderer::DrawTest()
{
    static unsigned int tex = LoadTexture("test.png");

    DrawTexture(tex,100,100,256,256);
}

void Renderer::BeginWorldDraw()
{
    // Push matrix và apply camera offset âm
    // Camera (camX, camY) là world position của góc trên-trái viewport
    // → dịch toàn bộ world sang trái/lên một lượng (camX, camY)
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(-_camX, -_camY, 0.f);
    _camActive = true;
}

void Renderer::EndWorldDraw()
{
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    _camActive = false;
}

void Renderer::DrawText(
    const std::string& text,
    float x,
    float y,
    float scale
)
{
    _text.DrawText2D(text, x, y, scale);
}