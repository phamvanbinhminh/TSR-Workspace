#pragma once
#include <string>
#include <memory>

class Texture
{
public:
    Texture();
    ~Texture();

    bool LoadFromFile(const std::string& filename);
    bool LoadFromMemory(const unsigned char* data, int width, int height, int channels);
    
    void Bind() const;
    void Unbind() const;

    unsigned int GetID() const { return _textureID; }
    int GetWidth() const { return _width; }
    int GetHeight() const { return _height; }

private:
    unsigned int _textureID;
    int _width;
    int _height;
    int _channels;
};