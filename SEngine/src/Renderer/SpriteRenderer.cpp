#include "SpriteRenderer.h"
#include <iostream>

SpriteRenderer::SpriteRenderer(Object* owner)
    : Component(owner)
    , _currentSpriteName("")
{
}

SpriteRenderer::~SpriteRenderer()
{
    RemoveAllSprites();
}

Sprite* SpriteRenderer::CreateSprite(const std::string& name)
{
    auto it = _sprites.find(name);
    if (it != _sprites.end())
    {
        std::cerr << "Sprite with name '" << name << "' already exists" << std::endl;
        return it->second.get();
    }

    auto sprite = std::make_unique<Sprite>(GetOwner());
    Sprite* spritePtr = sprite.get();
    _sprites[name] = std::move(sprite);

    if (_currentSpriteName.empty())
    {
        _currentSpriteName = name;
    }

    return spritePtr;
}

Sprite* SpriteRenderer::GetSprite(const std::string& name)
{
    auto it = _sprites.find(name);
    if (it != _sprites.end())
    {
        return it->second.get();
    }
    return nullptr;
}

void SpriteRenderer::RemoveSprite(const std::string& name)
{
    auto it = _sprites.find(name);
    if (it != _sprites.end())
    {
        _sprites.erase(it);
        
        if (_currentSpriteName == name)
        {
            _currentSpriteName = "";
            if (!_sprites.empty())
            {
                _currentSpriteName = _sprites.begin()->first;
            }
        }
    }
}

void SpriteRenderer::RemoveAllSprites()
{
    _sprites.clear();
    _currentSpriteName = "";
}

void SpriteRenderer::SetCurrentSprite(const std::string& name)
{
    if (_sprites.find(name) != _sprites.end())
    {
        _currentSpriteName = name;
    }
    else
    {
        std::cerr << "Sprite with name '" << name << "' does not exist" << std::endl;
    }
}

void SpriteRenderer::Update(float deltaTime)
{
    if (!_currentSpriteName.empty())
    {
        Sprite* sprite = GetSprite(_currentSpriteName);
        if (sprite)
        {
            sprite->Update(deltaTime);
        }
    }
}

void SpriteRenderer::Render()
{
    if (!_currentSpriteName.empty())
    {
        Sprite* sprite = GetSprite(_currentSpriteName);
        if (sprite)
        {
            sprite->Render();
        }
    }
}