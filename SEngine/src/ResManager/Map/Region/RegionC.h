#pragma once
#include <vector>
#include <string>
#include <set>
#include "../../../SExportEngineAPI.h"
#include "RegionData.h"

class Renderer;

// ── TileEntry: một tile đã đặt trong region ───────────────────
struct TileEntry
{
    std::string sprPath;    // đường dẫn .spr
    bool        useAnim   = false;
    int         animIdx   = 0;  // index animation
    int         frameIdx  = 0;  // index frame tĩnh
    float       x         = 0.f;
    float       y         = 0.f;
    int         layer     = 0;

    // Runtime — GPU texture (không lưu file)
    unsigned int texID   = 0;
    int          frameW  = 0;
    int          frameH  = 0;
    int          pivotX  = 0;
    int          pivotY  = 0;
    bool         loaded  = false;
};

// ── RegionC: client-side region data ──────────────────────────
// File format (region_C_y.dat):
//   "REGION_DATA_C" (13 bytes)
//   version: int = 4
//   [Obstacle Table]
//     obsCount: int
//     for each: x, y, w, h (float×4)
//   layerCount: int
//     tileCount: int
//       plen: int + sprPath: string
//       useAnim: uint8
//       animIdx: int
//       frameIdx: int
//       x: float, y: float
// ──────────────────────────────────────────────────────────────
class SENGINE_API RegionC
{
public:
    RegionC();
    RegionC(const RegionC&) = delete;
    RegionC& operator=(const RegionC&) = delete;
    RegionC(RegionC&&)            = default;
    RegionC& operator=(RegionC&&) = default;
    ~RegionC();

    bool Load(const std::string& file);
    void Unload();

    // Preload toàn bộ tiles lên GPU — gọi 1 lần sau Load(), không gọi trong render loop
    void PreloadTiles(Renderer& renderer);

    // Draw tất cả tile trên layer chỉ định
    // offsetX/Y = world-space offset của region này (regionX * unitSize, regionY * unitSize)
    void DrawLayer(Renderer& renderer, float offsetX, float offsetY, int layerIdx);

    int GetLayerCount() const { return (int)_layers.size(); }
    bool IsLoaded() const { return _loaded; }

    // Obstacle table (cùng như RegionS, để client có thể dùng khi cần)
    const std::vector<Obstacle>& GetObstacles() const { return _obstacles; }

private:
    bool LoadTile(TileEntry& tile, Renderer& renderer);

    std::vector<Obstacle>               _obstacles;
    std::vector<std::vector<TileEntry>> _layers; // [layerIdx][tileIdx]
    bool _loaded = false;
};
