#include "AppConfig.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════
// Singleton
// ═══════════════════════════════════════════════════════════
AppConfig& AppConfig::Get()
{
    static AppConfig inst;
    return inst;
}

// ═══════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════

// Thay '\\' -> '/' và encode pipe '|' -> \x01 để dùng làm separator trong list
std::string AppConfig::EscapeVal(const std::string& v)
{
    std::string r;
    r.reserve(v.size());
    for (char c : v)
    {
        if (c == '\\') r += '/';
        else           r += c;
    }
    return r;
}

std::string AppConfig::UnescapeVal(const std::string& v)
{
    // trim whitespace
    size_t s = v.find_first_not_of(" \t\r\n");
    size_t e = v.find_last_not_of(" \t\r\n");
    if (s == std::string::npos) return {};
    return v.substr(s, e - s + 1);
}

std::string AppConfig::GetExeDir()
{
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().generic_string();
#else
    return fs::current_path().generic_string();
#endif
}

// ═══════════════════════════════════════════════════════════
// Load
// ═══════════════════════════════════════════════════════════
bool AppConfig::Load(const std::string& path)
{
    _iniPath = path.empty() ? (GetExeDir() + "/resmanager.ini") : path;

    std::ifstream f(_iniPath);
    if (!f) return false; // file chua ton tai = ok, dung default

    std::string section;
    std::string line;
    while (std::getline(f, line))
    {
        // strip comment
        auto cpos = line.find(';');
        if (cpos != std::string::npos) line = line.substr(0, cpos);
        // trim
        auto trim = [](std::string& s){
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n");
            s = (a == std::string::npos) ? "" : s.substr(a, b-a+1);
        };
        trim(line);
        if (line.empty()) continue;

        // section header
        if (line.front() == '[' && line.back() == ']')
        {
            section = line.substr(1, line.size()-2);
            continue;
        }

        // key=value
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq+1);
        trim(key); trim(val);
        val = UnescapeVal(val);

        // ── [Map] ────────────────────────────────────────────
        if (section == "Map")
        {
            if (key == "LastPath")        mapLastPath      = val;
            else if (key == "VfsRoot")    mapVfsRoot       = val;
            else if (key == "ExportType") mapExportType    = std::stoi(val.empty()?"1":val);
            else if (key == "LoadPathS")  mapLoadPathS     = val;
            else if (key == "LoadPathC")  mapLoadPathC     = val;
            else if (key == "LoadMode")   mapLoadMode      = std::stoi(val.empty()?"0":val);
            else if (key == "TilesetFolder")
            {
                if (!val.empty()) mapTilesetFolders.push_back(val);
            }
        }
        // ── [Spr] ────────────────────────────────────────────
        else if (section == "Spr")
        {
            if      (key == "LastLoadPath")   sprLastLoadPath   = val;
            else if (key == "LastExportPath") sprLastExportPath = val;
            else if (key == "LastImportPath") sprLastImportPath = val;
        }
        // ── [Pak] ────────────────────────────────────────────
        else if (section == "Pak")
        {
            if      (key == "LastPath")       pakLastPath       = val;
            else if (key == "LastExtractDir") pakLastExtractDir = val;
            else if (key == "LastPackDir")    pakLastPackDir    = val;
        }
        // ── [Window] ─────────────────────────────────────────
        else if (section == "Window")
        {
            if      (key == "ShowMap") showMapEditor = (val == "1");
            else if (key == "ShowSpr") showSprEditor = (val == "1");
            else if (key == "ShowPak") showPakEditor = (val == "1");
        }
    }
    return true;
}

// ═══════════════════════════════════════════════════════════
// Save
// ═══════════════════════════════════════════════════════════
bool AppConfig::Save(const std::string& path)
{
    if (!path.empty()) _iniPath = path;
    if (_iniPath.empty()) _iniPath = GetExeDir() + "/resmanager.ini";

    std::ofstream f(_iniPath);
    if (!f) return false;

    f << "; ResManager Tool - App Config\n";
    f << "; Auto-generated, do not edit manually unless needed.\n\n";

    // ── [Map] ────────────────────────────────────────────────
    f << "[Map]\n";
    f << "LastPath="        << EscapeVal(mapLastPath)   << "\n";
    f << "VfsRoot="         << EscapeVal(mapVfsRoot)    << "\n";
    f << "ExportType="      << mapExportType            << "\n";
    f << "LoadPathS="       << EscapeVal(mapLoadPathS)  << "\n";
    f << "LoadPathC="       << EscapeVal(mapLoadPathC)  << "\n";
    f << "LoadMode="        << mapLoadMode              << "\n";
    for (const auto& tf : mapTilesetFolders)
        f << "TilesetFolder=" << EscapeVal(tf) << "\n";
    f << "\n";

    // ── [Spr] ────────────────────────────────────────────────
    f << "[Spr]\n";
    f << "LastLoadPath="   << EscapeVal(sprLastLoadPath)   << "\n";
    f << "LastExportPath=" << EscapeVal(sprLastExportPath) << "\n";
    f << "LastImportPath=" << EscapeVal(sprLastImportPath) << "\n";
    f << "\n";

    // ── [Pak] ────────────────────────────────────────────────
    f << "[Pak]\n";
    f << "LastPath="       << EscapeVal(pakLastPath)       << "\n";
    f << "LastExtractDir=" << EscapeVal(pakLastExtractDir) << "\n";
    f << "LastPackDir="    << EscapeVal(pakLastPackDir)    << "\n";
    f << "\n";

    // ── [Window] ─────────────────────────────────────────────
    f << "[Window]\n";
    f << "ShowMap=" << (showMapEditor ? 1 : 0) << "\n";
    f << "ShowSpr=" << (showSprEditor ? 1 : 0) << "\n";
    f << "ShowPak=" << (showPakEditor ? 1 : 0) << "\n";

    return true;
}
