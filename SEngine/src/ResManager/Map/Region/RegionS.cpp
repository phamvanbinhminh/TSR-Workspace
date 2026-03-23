#include "RegionS.h"
#include <fstream>
#include <filesystem>
#include <cstring>
#include <iostream>

bool RegionS::Save(const std::string& file) const
{
    // Tạo thư mục nếu chưa có
    std::filesystem::path p(file);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path());

    std::ofstream f(file, std::ios::binary);
    if (!f.is_open())
    {
        std::cerr << "[RegionS] Không thể ghi: " << file << "\n";
        return false;
    }

    // Magic
    f.write("REGION_DATA_S", 13);

    // version = 1
    int version = 1;
    f.write((char*)&version, sizeof(int));

    // obsCount
    int cnt = (int)_obstacles.size();
    f.write((char*)&cnt, sizeof(int));

    for (const auto& obs : _obstacles)
    {
        f.write((char*)&obs.x, sizeof(float));
        f.write((char*)&obs.y, sizeof(float));
        f.write((char*)&obs.w, sizeof(float));
        f.write((char*)&obs.h, sizeof(float));
    }

    return true;
}

bool RegionS::Load(const std::string& file)
{
    std::ifstream f(file, std::ios::binary);
    if (!f.is_open())
        return true; // region trống — OK

    char magic[14] = {};
    f.read(magic, 13);
    if (strncmp(magic, "REGION_DATA_S", 13) != 0)
    {
        std::cerr << "[RegionS] Magic header sai: " << file << "\n";
        return false;
    }

    int version = 0;
    f.read((char*)&version, sizeof(int));
    // version 1: obsCount + {x,y,w,h} * n
    // legacy (no version): treat as version 1 if magic OK

    int obsCount = 0;
    f.read((char*)&obsCount, sizeof(int));
    if (obsCount < 0 || obsCount > 100000) return false;

    _obstacles.resize(obsCount);
    for (int i = 0; i < obsCount; i++)
    {
        f.read((char*)&_obstacles[i].x, sizeof(float));
        f.read((char*)&_obstacles[i].y, sizeof(float));
        f.read((char*)&_obstacles[i].w, sizeof(float));
        f.read((char*)&_obstacles[i].h, sizeof(float));
    }

    return true;
}
