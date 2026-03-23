#include "Map.h"
#include "../../Renderer/Renderer.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <iostream>

// OpenGL cho DrawGrid
#include <GLFW/glfw3.h>

Map::Map() {}
Map::~Map() {}

// ── Helpers ───────────────────────────────────────────────────
void Map::WorldToRegion(float wx, float wy, int& rx, int& ry) const
{
    rx = (int)(wx / _unitSize);
    ry = (int)(wy / _unitSize);
}

bool Map::LoadMapFile(const std::string& file)
{
    std::ifstream f(file);
    if (!f.is_open()) return false;

    std::string line;
    while (std::getline(f, line))
    {
        // strip \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == '[') continue;
        if      (line.find("type=")     == 0)
        {
            std::string t = line.substr(5);
            if (t == "S" || t == "s") _mapType = MapType::S;
            else if (t == "C" || t == "c") _mapType = MapType::C;
        }
        else if (line.find("w=")        == 0) _width    = std::stoi(line.substr(2));
        else if (line.find("h=")        == 0) _height   = std::stoi(line.substr(2));
        else if (line.find("UnitSize=") == 0) _unitSize = std::stoi(line.substr(9));
    }
    return (_width > 0 && _height > 0 && _unitSize > 0);
}

bool Map::Load(const std::string& folder)
{
    _folder = folder;

    std::string mapFile =
        folder + "/" +
        std::filesystem::path(folder).filename().string() +
        ".map";

    printf("[Map] Loading map file: %s\n", mapFile.c_str());

    if (!LoadMapFile(mapFile))
    {
        std::cerr << "[Map] Không tìm thấy hoặc lỗi đọc: " << mapFile << "\n";
        return false;
    }

    _regionW = (_width  + _unitSize - 1) / _unitSize;
    _regionH = (_height + _unitSize - 1) / _unitSize;

    // Luôn load RegionS (obstacle/collision) nếu file tồn tại
    // Client không cần nhưng không hại gì khi load (sẽ bỏ qua nếu file không có)
    _serverRegions.resize(_regionW, std::vector<RegionS>(_regionH));
    int loadedS = 0;
    for (int rx = 0; rx < _regionW; rx++)
        for (int ry = 0; ry < _regionH; ry++)
            if (_serverRegions[rx][ry].Load(RegionSPath(rx, ry)))
                loadedS++;
    printf("[Map] RegionS loaded: %d/%d\n", loadedS, _regionW * _regionH);

    return true;
}

// ================================================================
//  CLIENT methods
// ================================================================

std::string Map::RegionCPath(int rx, int ry) const
{
    return _folder + "/Region/" +
           std::to_string(rx) + "/region_C_" +
           std::to_string(ry) + ".dat";
}

bool Map::IsRegionLoaded(int rx, int ry) const
{
    return _clientRegions.count(RegionKey(rx, ry)) > 0;
}

void Map::LoadRegionAround(Renderer& renderer, float wx, float wy, int radius)
{
    int cx, cy;
    WorldToRegion(wx, wy, cx, cy);
    
    for (int rx = cx - radius; rx <= cx + radius; rx++)
    {
        for (int ry = cy - radius; ry <= cy + radius; ry++)
        {
            if (rx < 0 || ry < 0 || rx >= _regionW || ry >= _regionH) continue;
            if (IsRegionLoaded(rx, ry)) continue;

            RegionC region;
            region.Load(RegionCPath(rx, ry));

            // Cập nhật maxLayers từ region đã load
            if (region.GetLayerCount() > _maxLayers)
                _maxLayers = region.GetLayerCount();

            _clientRegions.emplace(RegionKey(rx, ry), std::move(region));
        }
    }
}

void Map::UnloadRegionsFar(float wx, float wy, int unloadRadius)
{
    int cx, cy;
    WorldToRegion(wx, wy, cx, cy);

    for (auto it = _clientRegions.begin(); it != _clientRegions.end(); )
    {
        int key = it->first;
        int rx  = (_regionH > 0) ? key / _regionH : 0;
        int ry  = (_regionH > 0) ? key % _regionH : 0;

        int dx = std::abs(rx - cx);
        int dy = std::abs(ry - cy);

        if (dx > unloadRadius || dy > unloadRadius)
            it = _clientRegions.erase(it);
        else
            ++it;
    }
}

void Map::DrawLayer(Renderer& renderer, int layerIdx)
{
    for (auto& [key, region] : _clientRegions)
    {
        int rx = (_regionH > 0) ? key / _regionH : 0;
        int ry = (_regionH > 0) ? key % _regionH : 0;

        float offsetX = (float)(rx * _unitSize);
        float offsetY = (float)(ry * _unitSize);

        region.DrawLayer(renderer, offsetX, offsetY, layerIdx);
    }
}

void Map::DrawGrid(Renderer& renderer, int size)
{
    glColor3f(0.25f, 0.25f, 0.25f);
    glBegin(GL_LINES);
    for (int x = 0; x <= _width; x += size)
    {
        glVertex2f((float)x, 0.f);
        glVertex2f((float)x, (float)_height);
    }
    for (int y = 0; y <= _height; y += size)
    {
        glVertex2f(0.f, (float)y);
        glVertex2f((float)_width, (float)y);
    }
    glEnd();
}

// ================================================================
//  SERVER methods
// ================================================================

std::string Map::RegionSPath(int rx, int ry) const
{
    return _folder + "/Region/" +
           std::to_string(rx) + "/region_S_" +
           std::to_string(ry) + ".dat";
}

RegionS* Map::GetRegion(int rx, int ry)
{
    if (rx < 0 || ry < 0 || rx >= _regionW || ry >= _regionH)
        return nullptr;
    if (_serverRegions.empty()) return nullptr;
    return &_serverRegions[rx][ry];
}

// AABB overlap check
static bool AabbOverlap(float ax, float ay, float aw, float ah,
                        float bx, float by, float bw, float bh)
{
    return (ax < bx + bw) && (ax + aw > bx) &&
           (ay < by + bh) && (ay + ah > by);
}

bool Map::CheckCollision(float wx, float wy, float w, float h) const
{
    if (_serverRegions.empty()) return false;

    int rxMin = (int)((wx) / _unitSize);
    int ryMin = (int)((wy) / _unitSize);
    int rxMax = (int)((wx + w) / _unitSize);
    int ryMax = (int)((wy + h) / _unitSize);

    rxMin = std::max(rxMin, 0);
    ryMin = std::max(ryMin, 0);
    rxMax = std::min(rxMax, _regionW - 1);
    ryMax = std::min(ryMax, _regionH - 1);

    for (int rx = rxMin; rx <= rxMax; rx++)
    {
        for (int ry = ryMin; ry <= ryMax; ry++)
        {
            const RegionS& region = _serverRegions[rx][ry];
            float offX = (float)(rx * _unitSize);
            float offY = (float)(ry * _unitSize);

            for (const auto& obs : region.GetObstacles())
            {
                float obsWX = obs.x + offX;
                float obsWY = obs.y + offY;

                if (AabbOverlap(wx, wy, w, h, obsWX, obsWY, obs.w, obs.h))
                    return true;
            }
        }
    }
    return false;
}
