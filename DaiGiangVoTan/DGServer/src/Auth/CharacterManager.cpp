#include "CharacterManager.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

// ================================================================
//  Helpers
// ================================================================

static bool DirectoryExists(const std::string& path)
{
    struct stat st{};
    return (stat(path.c_str(), &st) == 0) && (st.st_mode & S_IFDIR);
}

static void EnsureDir(const std::string& path)
{
    if (!DirectoryExists(path))
        MKDIR(path.c_str());
}

// ================================================================
//  FilePath
// ================================================================

std::string CharacterManager::FilePath(const std::string& username) const
{
    return _dir + "/" + username + ".txt";
}

bool CharacterManager::Exists(const std::string& username) const
{
    std::ifstream f(FilePath(username));
    return f.good();
}

// ================================================================
//  Load
// ================================================================

CharacterData CharacterManager::Load(const std::string& username)
{
    CharacterData data{};
    data.appearance = DefaultAppearance();

    std::ifstream file(FilePath(username));
    if (!file.is_open()) return data;

    std::string line;
    while (std::getline(file, line))
    {
        // Bỏ comment và dòng trống
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        try
        {
            if      (key == "bodyID")      data.appearance.bodyID      = (uint16_t)std::stoi(val);
            else if (key == "hairID")      data.appearance.hairID      = (uint16_t)std::stoi(val);
            else if (key == "topID")       data.appearance.topID       = (uint16_t)std::stoi(val);
            else if (key == "bottomID")    data.appearance.bottomID    = (uint16_t)std::stoi(val);
            else if (key == "rightHandID") data.appearance.rightHandID = (uint16_t)std::stoi(val);
            else if (key == "leftHandID")  data.appearance.leftHandID  = (uint16_t)std::stoi(val);
            else if (key == "helmetID")    data.appearance.helmetID    = (uint16_t)std::stoi(val);
            else if (key == "wingID")      data.appearance.wingID      = (uint16_t)std::stoi(val);
            else if (key == "posX")        data.posX                   = std::stof(val);
            else if (key == "posY")        data.posY                   = std::stof(val);
            else if (key == "level")       data.level                  = std::stoi(val);
            else if (key == "mapID")       data.mapID                  = std::stoi(val);
        }
        catch (...) {}
    }

    return data;
}

// ================================================================
//  LoadOrCreate
// ================================================================

CharacterData CharacterManager::LoadOrCreate(const std::string& username)
{
    if (Exists(username))
    {
        CharacterData data = Load(username);
        std::cout << "[CharacterManager] Load character: " << username << "\n";
        return data;
    }
    else
    {
        CharacterData data{};
        data.appearance = DefaultAppearance();
        data.posX  = 640.f;
        data.posY  = 360.f;
        data.level = 1;
        Save(username, data);
        std::cout << "[CharacterManager] Tạo character mới: " << username << "\n";
        return data;
    }
}

// ================================================================
//  Save
// ================================================================

void CharacterManager::Save(const std::string& username, const CharacterData& data)
{
    EnsureDir(_dir);

    std::ofstream file(FilePath(username));
    if (!file.is_open())
    {
        std::cerr << "[CharacterManager] Không thể lưu: " << FilePath(username) << "\n";
        return;
    }

    file << "# Character data for " << username << "\n";
    file << "bodyID="      << data.appearance.bodyID      << "\n";
    file << "hairID="      << data.appearance.hairID      << "\n";
    file << "topID="       << data.appearance.topID       << "\n";
    file << "bottomID="    << data.appearance.bottomID    << "\n";
    file << "rightHandID=" << data.appearance.rightHandID << "\n";
    file << "leftHandID="  << data.appearance.leftHandID  << "\n";
    file << "helmetID="    << data.appearance.helmetID    << "\n";
    file << "wingID="      << data.appearance.wingID      << "\n";
    file << "posX="        << data.posX                   << "\n";
    file << "posY="        << data.posY                   << "\n";
    file << "level="       << data.level                  << "\n";
    file << "mapID="       << data.mapID                  << "\n";

    std::cout << "[CharacterManager] Saved: " << FilePath(username) << "\n";
}
