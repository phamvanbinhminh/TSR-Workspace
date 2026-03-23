#pragma once
#include <vector>
#include <string>
#include "../../../SExportEngineAPI.h"
#include "RegionData.h"

// ── RegionS: server-side region data ──────────────────────────
// File format (region_S_y.dat):
//   "REGION_DATA_S" (13 bytes)
//   version: int = 1
//   obsCount: int
//   for each: x, y, w, h (float×4)
// ──────────────────────────────────────────────────────────────
class SENGINE_API RegionS
{
public:
    bool Load(const std::string& file);
    bool Save(const std::string& file) const;

    const std::vector<Obstacle>& GetObstacles() const { return _obstacles; }
    std::vector<Obstacle>& GetObstacles() { return _obstacles; }

    void SetObstacles(const std::vector<Obstacle>& obs) { _obstacles = obs; }
    void AddObstacle(const Obstacle& obs) { _obstacles.push_back(obs); }
    void Clear() { _obstacles.clear(); }

private:
    std::vector<Obstacle> _obstacles;
};
