#pragma once
#include "../Component.h"
#include "../../ResManager/Spr/Spr.h"
#include "../../SExportEngineAPI.h"

#include <string>
#include <vector>
#include <cstdint>

// Forward declaration
class Renderer;

// SpriteComponent: bọc SprLoadedData (từ SprReader).
// - LoadFromSPR() nạp file .spr → upload textures lên GPU
// - PlayAnimation() / Update() điều khiển playback
// - Render() được gọi bởi RenderSystem (không gọi trực tiếp từ component loop)
class SENGINE_API SpriteComponent : public Component
{
public:
    SpriteComponent(Object* owner, Renderer* renderer);
    ~SpriteComponent();

    // Nạp file .spr từ disk → giải mã → upload GPU textures
    // xorKey: 4-byte key (truyền nullptr để không mã hoá)
    bool LoadFromSPR(const std::string& sprFile, const uint8_t xorKey[4] = nullptr);

    // Nạp từ buffer bytes (từ VFS/PAK)
    bool LoadFromBuffer(const std::vector<uint8_t>& buffer,
                        const uint8_t xorKey[4] = nullptr);

    // Nạp từ VFS (tự động ReadFile rồi LoadFromBuffer)
    bool LoadFromVFS(const std::string& virtualPath,
                     const uint8_t xorKey[4] = nullptr);

    // Animation control
    void PlayAnimation(const std::string& name, bool forceRestart = false);
    void StopAnimation();
    void PauseAnimation();
    void ResumeAnimation();
    void SetSpeed(float multiplier);  // 1.0 = normal

    // Render tại vị trí (x, y) với scale
    // Được gọi bởi RenderSystem
    void RenderAt(float x, float y, float scaleX = 1.f, float scaleY = 1.f,
                  bool flipX = false, bool flipY = false);

    // Playback update (advance frame)
    void Update(float dt) override;

    // Getters
    const std::string& GetCurrentAnimation() const { return _currentAnim; }
    int  GetCurrentFrame() const { return _currentFrame; }
    bool IsPlaying()  const { return _playing && !_paused; }
    bool IsLoaded()   const { return _loaded; }

    // Size của frame hiện tại
    int  GetFrameWidth()  const;
    int  GetFrameHeight() const;

    // Hitbox của frame hiện tại (từ SprFrameMeta)
    // Trả về true nếu hitbox hợp lệ (w > 0 && h > 0)
    bool GetHitbox(int& outX, int& outY, int& outW, int& outH) const
    {
        if (!_loaded || _currentFrame < 0 ||
            _currentFrame >= (int)_sprData.frames.size()) return false;
        const SprFrameMeta& meta = _sprData.frames[_currentFrame].metadata;
        outX = meta.hitboxX;
        outY = meta.hitboxY;
        outW = meta.hitboxWidth;
        outH = meta.hitboxHeight;
        return (outW > 0 && outH > 0);
    }

private:
    void UploadTextures();
    int  FindAnimation(const std::string& name) const;

    Renderer*        _renderer;
    SprLoadedData    _sprData;
    std::vector<unsigned int> _textureIDs;  // GPU texture per-frame (GLuint)

    // Playback state
    std::string  _currentAnim;
    int          _animIndex   = -1;   // index trong _sprData.animations
    int          _currentFrame = 0;   // index trong _sprData.frames
    float        _frameTimer  = 0.f;
    bool         _playing     = false;
    bool         _paused      = false;
    float        _speed       = 1.f;

    bool         _loaded      = false;
};
