#pragma once
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <unordered_map>
#include <cstdint>

#include "ResManager/Spr/Spr.h"

struct SprFrame
{
    std::string  name;
    std::string  sourcePath;
    unsigned int textureID = 0;
    int  width = 0, height = 0;
    int  offsetX = 0, offsetY = 0;
    int  duration = 33;
    SprFrameMeta metadata{};
    std::vector<uint8_t> pixels; // RGBA8
};

struct SprAnimation
{
    std::string      name;
    std::vector<int> frameIndices;
    int  fps  = 30;
    bool loop = true;
};

class SprEditor
{
public:
    SprEditor();
    ~SprEditor();

    void RenderEditor();
    void HandleDragDrop();

    bool ImportFolder(const std::string& folderPath);
    bool ImportGIF(const std::string& gifPath);
    void AddFrame(const std::string& imagePath);
    void AddFramesFromTileset(const std::string& imagePath, int tileW, int tileH);
    void RemoveFrame(int frameIndex);
    void MoveFrame(int fromIndex, int toIndex);

    SprAnimation* CreateAnimation(const std::string& name);
    SprAnimation* GetAnimation(const std::string& name);
    void RemoveAnimation(const std::string& name);
    void AddFrameToAnimation(const std::string& animName, int frameIndex);
    void RemoveFrameFromAnimation(const std::string& animName, int frameIndex);

    void SetFrameMetadata(int frameIndex, const SprFrameMeta& meta);
    void SetPivotPoint(int x, int y);

    bool LoadSPR(const std::string& sprPath);
    bool ExportSPR(const std::string& outputPath);

    void SetCurrentAnimation(const std::string& name);
    void Play();
    void Pause();
    void Stop();
    void SetFPS(int fps);
    void SetLoop(bool loop);

private:
    void RenderFrameList();
    void RenderAnimationList();
    void RenderPreview();
    void RenderProperties();
    void RenderExportOptions();

    unsigned int LoadTextureFromFile(const std::string& path, int& outW, int& outH);
    void UpdateFrameThumbnails();
    void UpdatePlayback(float dt);
    void ParseXorKey();
    SprBuildOptions MakeBuildOptions() const;

    // ── data ───────────────────────────────────────────────
    std::vector<SprFrame>      _frames;
    std::vector<SprAnimation>  _animations;
    std::unordered_map<std::string, int> _animationMap;

    // ── playback ───────────────────────────────────────────
    std::string _currentAnimation;
    int   _currentFrameIndex = 0;
    float _frameTimer        = 0.f;
    bool  _isPlaying         = false;
    bool  _isLooping         = true;
    int   _fps               = 30;
    int   _pivotX = 0, _pivotY = 0;

    // ── ui state ───────────────────────────────────────────
    int   _selectedFrame     = -1;
    int   _selectedAnimation = -1;
    bool  _showProperties    = false;
    float _previewScale      = 1.f;

    // ── overlay toggles ───────────────────────────────────
    bool _showPivot      = true;
    bool _showHitbox     = true;
    bool _showAttackBox  = true;
    bool _showOffset     = false;

    // ── bulk-edit state ────────────────────────────────────
    // "apply to all frames" values – edited per-property
    int  _bulkOffsetX  = 0, _bulkOffsetY  = 0;
    int  _bulkDuration = 33;
    int  _bulkHbX=0,  _bulkHbY=0,  _bulkHbW=0,  _bulkHbH=0;
    int  _bulkAbX=0,  _bulkAbY=0,  _bulkAbW=0,  _bulkAbH=0;

    // ── dialogs ────────────────────────────────────────────
    char _importPathBuf[512]{};
    char _exportPathBuf[512]{};
    char _loadPathBuf[512]{};
    char _newAnimNameBuf[128]{};
    bool _showImportDialog  = false;
    bool _showExportDialog  = false;
    bool _showLoadDialog    = false;
    bool _showNewAnimDialog = false;

    // ── status ─────────────────────────────────────────────
    char  _statusMsg[256]{};
    float _statusTimer = 0.f;

    // ── build / export options ─────────────────────────────
    bool _optRLE        = false;
    bool _optZlib       = false;
    bool _optPalette    = false;
    bool _optAtlas      = false;
    bool _optSkipAlpha  = false;
    bool _optEncryptXOR = false;
    char _xorKeyHex[16] = "53505230";
    int  _paletteSize   = 256;
    int  _atlasMaxW     = 2048;
    int  _atlasMaxH     = 2048;
    uint8_t _xorKey[4]  = {0x53, 0x50, 0x52, 0x30};
};
