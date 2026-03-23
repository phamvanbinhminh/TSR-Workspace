#pragma once
#include "Object.h"
#include "Sprite.h"
#include "../Renderer/Renderer.h"
#include <unordered_map>

class SpriteRenderer : public Component
{
public:
    SpriteRenderer(Object* owner);
    ~SpriteRenderer();

    // Sprite management
    Sprite* CreateSprite(const std::string& name);
    Sprite* GetSprite(const std::string& name);
    void RemoveSprite(const std::string& name);
    void RemoveAllSprites();

    // Rendering
    void SetCurrentSprite(const std::string& name);
    const std::string& GetCurrentSpriteName() const { return _currentSpriteName; }

    // Component interface
    void Update(float deltaTime) override;
    void Render() override;

private:
    std::unordered_map<std::string, std::unique_ptr<Sprite>> _sprites;
    std::string _currentSpriteName;
};