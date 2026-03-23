#include "SpriteComponent.h"
#include "../Object/Object.h"
#include "../../Renderer/Renderer.h"
#include "../../ResManager/Spr/Spr.h"
#include "../../ResManager/VFS/VFS.h"

#include <windows.h>
#include <gl/GL.h>
#include <gl/GLU.h>

// GL_CLAMP_TO_EDGE không có trong gl/GL.h của Windows — định nghĩa thủ công
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <iostream>
#include <algorithm>
#include <cstring>
#include <cmath>

SpriteComponent::SpriteComponent(Object* owner, Renderer* renderer)
    : Component(owner)
    , _renderer(renderer)
{
}

SpriteComponent::~SpriteComponent()
{
    // Xoá GPU textures
    if (!_textureIDs.empty())
    {
        glDeleteTextures((GLsizei)_textureIDs.size(),
                         _textureIDs.data());
        _textureIDs.clear();
    }
}

bool SpriteComponent::LoadFromSPR(const std::string& sprFile, const uint8_t xorKey[4])
{
    SprReader reader;
    if (!reader.LoadFromFile(sprFile, _sprData, xorKey))
    {
        std::cerr << "[SpriteComponent] Failed to load: " << sprFile << "\n";
        return false;
    }
    UploadTextures();
    _loaded = true;

    // Tự động play animation đầu tiên nếu có
    if (!_sprData.animations.empty())
        PlayAnimation(_sprData.animations[0].name, true);

    return true;
}

bool SpriteComponent::LoadFromBuffer(const std::vector<uint8_t>& buffer,
                                     const uint8_t xorKey[4])
{
    if (buffer.empty())
    {
        std::cerr << "[SpriteComponent] LoadFromBuffer: buffer rỗng\n";
        return false;
    }
    SprReader reader;
    if (!reader.LoadFromBuffer(buffer.data(), buffer.size(), _sprData, xorKey))
    {
        std::cerr << "[SpriteComponent] LoadFromBuffer: parse SPR thất bại\n";
        return false;
    }
    UploadTextures();
    _loaded = true;
    if (!_sprData.animations.empty())
        PlayAnimation(_sprData.animations[0].name, true);
    return true;
}

bool SpriteComponent::LoadFromVFS(const std::string& virtualPath,
                                   const uint8_t xorKey[4])
{
    auto bytes = VFS::Get().ReadFile(virtualPath);
    if (bytes.empty())
    {
        std::cerr << "[SpriteComponent] VFS not found: " << virtualPath << "\n";
        return false;
    }
    return LoadFromBuffer(bytes, xorKey);
}

void SpriteComponent::UploadTextures()
{
    // Xoá textures cũ nếu có
    if (!_textureIDs.empty())
    {
        glDeleteTextures((GLsizei)_textureIDs.size(), _textureIDs.data());
        _textureIDs.clear();
    }

    _textureIDs.resize(_sprData.frames.size(), 0);

    for (size_t i = 0; i < _sprData.frames.size(); ++i)
    {
        const SprLoadedFrame& frame = _sprData.frames[i];
        if (frame.pixels.empty() || frame.width <= 0 || frame.height <= 0)
            continue;

        unsigned int tid = 0;
        glGenTextures(1, &tid);
        glBindTexture(GL_TEXTURE_2D, tid);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     frame.width, frame.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE,
                     frame.pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        _textureIDs[i] = tid;
    }
}

// ── Animation ────────────────────────────────────────────────────────────────

int SpriteComponent::FindAnimation(const std::string& name) const
{
    for (int i = 0; i < (int)_sprData.animations.size(); ++i)
        if (_sprData.animations[i].name == name)
            return i;
    return -1;
}

void SpriteComponent::PlayAnimation(const std::string& name, bool forceRestart)
{
    if (!_loaded) return;
    if (_currentAnim == name && _playing && !forceRestart) return;

    int idx = FindAnimation(name);
    if (idx < 0)
    {
        std::cerr << "[SpriteComponent] Animation not found: " << name << "\n";
        return;
    }
    _currentAnim  = name;
    _animIndex    = idx;
    _frameTimer   = 0.f;
    _playing      = true;
    _paused       = false;
    // set current frame = startFrame của animation
    _currentFrame = (int)_sprData.animations[idx].startFrame;
}

void SpriteComponent::StopAnimation()
{
    _playing = false;
    _paused  = false;
    _frameTimer = 0.f;
}

void SpriteComponent::PauseAnimation()  { _paused = true; }
void SpriteComponent::ResumeAnimation() { _paused = false; }
void SpriteComponent::SetSpeed(float multiplier)
{
    _speed = (multiplier > 0.f) ? multiplier : 1.f;
}

// ── Update (advance frame) ───────────────────────────────────────────────────

void SpriteComponent::Update(float dt)
{
    if (!_loaded || !_playing || _paused) return;
    if (_animIndex < 0 || _animIndex >= (int)_sprData.animations.size()) return;

    const SprLoadedAnim& anim = _sprData.animations[_animIndex];
    int fps = (anim.fps > 0) ? (int)anim.fps : 12;
    float frameDuration = (1.f / (float)fps) / _speed;

    _frameTimer += dt;
    if (_frameTimer >= frameDuration)
    {
        _frameTimer -= frameDuration;

        int localFrame = _currentFrame - (int)anim.startFrame;
        localFrame++;
        if (localFrame >= (int)anim.frameCount)
        {
            if (anim.loop)
                localFrame = 0;
            else
            {
                localFrame = (int)anim.frameCount - 1;
                _playing = false;
            }
        }
        _currentFrame = (int)anim.startFrame + localFrame;
    }
}

// ── Render ───────────────────────────────────────────────────────────────────

void SpriteComponent::RenderAt(float x, float y,
                                float scaleX, float scaleY,
                                bool /*flipX*/, bool /*flipY*/)
{
    if (!_loaded || !_renderer) return;
    if (_currentFrame < 0 || _currentFrame >= (int)_sprData.frames.size()) return;

    const SprLoadedFrame& frame = _sprData.frames[_currentFrame];
    unsigned int tid = (_currentFrame < (int)_textureIDs.size())
                       ? _textureIDs[_currentFrame] : 0;
    if (tid == 0) return;

    float drawX = x + frame.offsetX * scaleX;
    float drawY = y + frame.offsetY * scaleY;
    float drawW = frame.width  * scaleX;
    float drawH = frame.height * scaleY;

    _renderer->DrawTexture(tid, drawX, drawY, drawW, drawH);
}

// ── Getters ──────────────────────────────────────────────────────────────────

int SpriteComponent::GetFrameWidth() const
{
    if (!_loaded || _currentFrame < 0 ||
        _currentFrame >= (int)_sprData.frames.size()) return 0;
    return _sprData.frames[_currentFrame].width;
}

int SpriteComponent::GetFrameHeight() const
{
    if (!_loaded || _currentFrame < 0 ||
        _currentFrame >= (int)_sprData.frames.size()) return 0;
    return _sprData.frames[_currentFrame].height;
}
