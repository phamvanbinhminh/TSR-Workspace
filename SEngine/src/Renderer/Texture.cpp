#include "Texture.h"
#include <stb_image.h>
#include <GL/glew.h>
#include <iostream>

Texture::Texture()
    : _textureID(0)
    , _width(0)
    , _height(0)
    , _channels(0)
{
}

Texture::~Texture()
{
    if (_textureID != 0)
    {
        glDeleteTextures(1, &_textureID);
    }
}

bool Texture::LoadFromFile(const std::string& filename)
{
    // Free existing texture
    if (_textureID != 0)
    {
        glDeleteTextures(1, &_textureID);
        _textureID = 0;
    }

    // Load image
    int width, height, channels;
    unsigned char* data = stbi_load(filename.c_str(), &width, &height, &channels, 0);
    
    if (!data)
    {
        std::cerr << "Failed to load texture: " << filename << std::endl;
        return false;
    }

    bool success = LoadFromMemory(data, width, height, channels);
    stbi_image_free(data);
    
    return success;
}

bool Texture::LoadFromMemory(const unsigned char* data, int width, int height, int channels)
{
    _width = width;
    _height = height;
    _channels = channels;

    // Generate texture
    glGenTextures(1, &_textureID);
    glBindTexture(GL_TEXTURE_2D, _textureID);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Determine format
    GLenum format;
    if (channels == 1)
        format = GL_RED;
    else if (channels == 3)
        format = GL_RGB;
    else if (channels == 4)
        format = GL_RGBA;
    else
    {
        std::cerr << "Unsupported number of channels: " << channels << std::endl;
        return false;
    }

    // Upload texture data
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void Texture::Bind() const
{
    glBindTexture(GL_TEXTURE_2D, _textureID);
}

void Texture::Unbind() const
{
    glBindTexture(GL_TEXTURE_2D, 0);
}