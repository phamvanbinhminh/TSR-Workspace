#pragma once
#include <string>
#include <vector>
#include <unordered_map>

#include "Region/RegionC.h"
#include "Region/RegionS.h"

// Forward decl – tránh include Renderer.h ở đây
class Renderer;

class SENGINE_API Map
{
public:
    enum class MapType { Unknown, S, C };

    Map();
    ~Map();

    // RegionC は non-copyable なので Map も non-copyable
    Map(const Map&)            = delete;
    Map& operator=(const Map&) = delete;
    Map(Map&&)                 = default;
    Map& operator=(Map&&)      = default;

    // Client: chỉ đọc .map header, RegionC lazy-load qua LoadRegionAround()
    bool Load(const std::string& folder);
    // Server: đọc header + load toàn bộ RegionS (obstacle data)
    bool LoadServer(const std::string& folder);
    bool LoadMapFile(const std::string& file);
    bool LoadMapFromFolder(const std::string& folder);

    int GetWidth()    const { return _width;    }
    int GetHeight()   const { return _height;   }
    int GetUnitSize() const { return _unitSize; }
    int GetRegionW()  const { return _regionW;  }
    int GetRegionH()  const { return _regionH;  }
    MapType GetMapType() const { return _mapType; }

    // Chuyển world pos → region index
    void WorldToRegion(float wx, float wy, int& rx, int& ry) const;

    // ── CLIENT methods ──────────────────────────────────────
    // Lazy-load các region trong vùng radius quanh (wx,wy)
    void LoadRegionAround(Renderer& renderer, float wx, float wy, int radius = 1);

    // Unload region nằm ngoài unloadRadius
    void UnloadRegionsFar(float wx, float wy, int unloadRadius = 2);

    // Vẽ tất cả region đã load, chỉ layer layerIdx
    void DrawLayer(Renderer& renderer, int layerIdx);

    // Vẽ grid debug
    void DrawGrid(Renderer& renderer, int size);

    // Vẽ tất cả obstacle của các region đã load dạng //// màu đỏ (debug)
    void DrawObstaclesDebug(Renderer& renderer);

    // Tổng số layer (lấy từ region đầu tiên được load)
    int GetLayerCount() const { return _maxLayers; }

    // ── SERVER methods ──────────────────────────────────────
    RegionS* GetRegion(int rx, int ry);

    // Kiểm tra AABB (wx,wy,w,h) có va chạm với obstacle trong map không
    bool CheckCollision(float wx, float wy, float w, float h) const;

    // Client-side collision check dùng _clientRegions (RegionC obstacle data)
    bool CheckCollisionClient(float wx, float wy, float w, float h) const;

private:


    int     _width    = 0;
    int     _height   = 0;
    int     _unitSize = 0;
    int     _regionW  = 0;
    int     _regionH  = 0;
    MapType _mapType  = MapType::Unknown;

                                  std::string _folder;

    // ── CLIENT data ──────────────────────────────────────────
    // Sparse map: regionIndex (rx * _regionH + ry) → RegionC
    std::unordered_map<int, RegionC> _clientRegions;
    int _maxLayers = 4; // mặc định 4 layers

    int  RegionKey(int rx, int ry) const { return rx * _regionH + ry; }
    bool IsRegionLoaded(int rx, int ry) const;
    std::string RegionCPath(int rx, int ry) const;

    // ── SERVER data ──────────────────────────────────────────
    std::vector<std::vector<RegionS>> _serverRegions;
    std::string RegionSPath(int rx, int ry) const;
};
