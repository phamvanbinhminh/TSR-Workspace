#include "RegionC.h"
#include "../../../Renderer/Renderer.h"
#include "../../Spr/Spr.h"

#include <fstream>
#include <cstring>
#include <iostream>
#include <filesystem>
#include <unordered_map>

RegionC::RegionC() {}

RegionC::~RegionC()
{
    _layers.clear();
    _obstacles.clear();
    _loaded = false;
}

void RegionC::Unload()
{
    _layers.clear();
    _obstacles.clear();
    _loaded = false;
}

// ── Helper: đọc obstacle table ───────────────────────────────
static void ReadObstacles(std::ifstream& f, std::vector<Obstacle>& out)
{
    int obsCount = 0;
    f.read((char*)&obsCount, sizeof(int));
    if (obsCount < 0 || obsCount > 100000) { obsCount = 0; return; }
    out.resize(obsCount);
    for (int i = 0; i < obsCount; i++)
    {
        f.read((char*)&out[i].x, sizeof(float));
        f.read((char*)&out[i].y, sizeof(float));
        f.read((char*)&out[i].w, sizeof(float));
        f.read((char*)&out[i].h, sizeof(float));
    }
}

// ── Helper: đọc layer tiles ──────────────────────────────────
static void ReadLayers(std::ifstream& f, std::vector<std::vector<TileEntry>>& layers)
{
    int layerCount = 0;
    f.read((char*)&layerCount, sizeof(int));
    if (layerCount <= 0 || layerCount > 64) return;
    layers.resize(layerCount);

    for (int l = 0; l < layerCount; l++)
    {
        int tileCount = 0;
        f.read((char*)&tileCount, sizeof(int));
        if (tileCount < 0 || tileCount > 500000) continue;
        layers[l].resize(tileCount);

        for (int ti = 0; ti < tileCount; ti++)
        {
            TileEntry& tile = layers[l][ti];
            tile.layer = l;

            int plen = 0;
            f.read((char*)&plen, sizeof(int));
            if (plen > 0 && plen < 1024)
            {
                tile.sprPath.resize(plen);
                f.read(&tile.sprPath[0], plen);
            }

            uint8_t ua = 0;
            f.read((char*)&ua, 1);
            tile.useAnim = (ua != 0);

            f.read((char*)&tile.animIdx,  sizeof(int));
            f.read((char*)&tile.frameIdx, sizeof(int));
            f.read((char*)&tile.x,        sizeof(float));
            f.read((char*)&tile.y,        sizeof(float));
        }
    }
}

// ── Load region_C_y.dat ───────────────────────────────────────
// Version 4 (current):
//   "REGION_DATA_C" (13 bytes)
//   version: int = 4
//   [Obstacle Table]: obsCount + {x,y,w,h}*n
//   layerCount: int
//   for each layer: tileCount + tiles
//
// Version 3 (no obstacles):
//   "REGION_DATA_C" (13 bytes)
//   version: int = 3
//   layerCount: int
//   for each layer: tileCount + tiles
//
// Version 2 (legacy - obstacles then layers):
//   "REGION_DATA_C" (13 bytes)
//   version: int = 2
//   obsCount + obstacles
//   layerCount + layers
// ─────────────────────────────────────────────────────────────
bool RegionC::Load(const std::string& file)
{
    std::ifstream f(file, std::ios::binary);
    if (!f.is_open())
    {
        // File không tồn tại là bình thường (region trống)
        _loaded = true;
        return true;
    }

    char magic[14] = {};
    f.read(magic, 13);
    if (strncmp(magic, "REGION_DATA_C", 13) != 0)
    {
        std::cerr << "[RegionC] Magic header sai: " << file << "\n";
        return false;
    }

    int version = 0;
    f.read((char*)&version, sizeof(int));

    ReadObstacles(f, _obstacles);
    ReadLayers(f, _layers);

    _loaded = true;
    return true;
}

// ── Global texture cache: sprPath+":"+frameIdx → texID + pivot/size ─
struct CachedTexInfo
{
    unsigned int texID  = 0;
    int          frameW = 0;
    int          frameH = 0;
    int          pivotX = 0;
    int          pivotY = 0;
};
static std::unordered_map<std::string, CachedTexInfo> s_texCache;

// ── LoadTile: upload 1 frame vào GPU texture ─────────────────
// Thứ tự tìm file:
//   1. tile.sprPath trực tiếp (nếu absolute và tồn tại — editor/dev mode)
//   2. res/paks/res/<sprPath>   (client disk mode)
// SPR cùng path+frame được cache lại — không load lại lên GPU.
// ─────────────────────────────────────────────────────────────
bool RegionC::LoadTile(TileEntry& tile, Renderer& renderer)
{
    if (tile.loaded)
        return true;

    if (tile.sprPath.empty())
    {
        std::cerr << "[LoadTile] ❌ Empty sprPath\n";
        tile.loaded = true;
        return false;
    }

    namespace fs = std::filesystem;

    // Normalize path
    std::string normPath = tile.sprPath;
    for (auto& c : normPath) if (c == '\\') c = '/';

    std::string resolvedPath;

    // Resolve path
    if (fs::exists(normPath))
    {
        resolvedPath = normPath;
    }
    else
    {
        std::string clientPath = normPath;
        if (fs::exists(clientPath))
            resolvedPath = clientPath;
    }

    if (resolvedPath.empty())
    {
        std::cerr << "[LoadTile] ❌ File not found: " << normPath << "\n";
        tile.loaded = true;
        return false;
    }

    // Cache key
    std::string cacheKey = resolvedPath + (tile.useAnim
        ? ":a" + std::to_string(tile.animIdx)
        : ":f" + std::to_string(tile.frameIdx));

    auto it = s_texCache.find(cacheKey);
    if (it != s_texCache.end())
    {
        tile.texID  = it->second.texID;
        tile.frameW = it->second.frameW;
        tile.frameH = it->second.frameH;
        tile.pivotX = it->second.pivotX;
        tile.pivotY = it->second.pivotY;
        tile.loaded = true;
        return (tile.texID != 0);
    }

    // Load SPR
    SprReader reader;
    SprLoadedData data;

    if (!reader.LoadFromFile(resolvedPath, data, nullptr))
    {
        std::cerr << "[LoadTile] ❌ Failed to load SPR: " << resolvedPath << "\n";
        tile.loaded = true;
        return false;
    }

    tile.pivotX = data.header.pivotX;
    tile.pivotY = data.header.pivotY;

    int fi = 0;
    if (tile.useAnim && tile.animIdx < (int)data.animations.size())
        fi = data.animations[tile.animIdx].startFrame;
    else
        fi = tile.frameIdx;

    if (fi < 0 || fi >= (int)data.frames.size())
    {
        std::cerr << "[LoadTile] ❌ Invalid frame index: " << fi
                  << " | total=" << data.frames.size() << "\n";
        tile.loaded = true;
        return false;
    }

    const auto& fr = data.frames[fi];

    if (fr.pixels.empty())
    {
        std::cerr << "[LoadTile] ❌ Empty pixel data: " << resolvedPath << "\n";
        tile.loaded = true;
        return false;
    }

    tile.texID = renderer.LoadTextureRawRGBA(
        const_cast<unsigned char*>(fr.pixels.data()),
        fr.width,
        fr.height
    );

    if (tile.texID == 0)
    {
        std::cerr << "[LoadTile] ❌ Failed to create texture: " << resolvedPath << "\n";
        tile.loaded = true;
        return false;
    }

    tile.frameW = fr.width;
    tile.frameH = fr.height;

    // Cache store
    s_texCache[cacheKey] = {
        tile.texID,
        tile.frameW,
        tile.frameH,
        tile.pivotX,
        tile.pivotY
    };

    tile.loaded = true;
    return true;
}
// ── DrawLayer ─────────────────────────────────────────────────
void RegionC::DrawLayer(Renderer& renderer, float offsetX, float offsetY, int layerIdx)
{
    if (layerIdx < 0 || layerIdx >= (int)_layers.size()) return;

    for (TileEntry& tile : _layers[layerIdx])
    {
        if (!tile.loaded)
            if(!LoadTile(tile, renderer)){
                std::cerr << "[RegionC] Không load được tile: " << tile.sprPath << "\n";
                continue;
            }
        
        // world position = tile.x + offsetX, tile.y + offsetY
        // pivot offset: vẽ từ góc trên-trái = (pos - pivot)
        // float drawX = tile.x + offsetX - tile.pivotX;
        // float drawY = tile.y + offsetY - tile.pivotY;
        float drawX = offsetX - tile.pivotX;
        float drawY = offsetY - tile.pivotY;

        renderer.DrawTexture(tile.texID, drawX, drawY,
                             (float)tile.frameW, (float)tile.frameH);
    }
}
