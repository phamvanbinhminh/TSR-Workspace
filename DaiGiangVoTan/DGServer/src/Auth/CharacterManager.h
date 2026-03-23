#pragma once
#include <string>
#include "../../CommonProtocol/Appearance.h"

// ================================================================
//  CharacterData — dữ liệu nhân vật lưu trên server
//  File: characters/{username}.txt
//  Format (key=value, dễ đọc/chỉnh tay):
//      bodyID=1
//      hairID=1
//      topID=0
//      bottomID=0
//      rightHandID=0
//      leftHandID=0
//      helmetID=0
//      wingID=0
//      posX=640.0
//      posY=360.0
//      level=1
// ================================================================
struct CharacterData
{
    Appearance appearance;
    float      posX  = 640.f;
    float      posY  = 360.f;
    int        level = 1;
    int        mapID = 0;   // 0 = map mặc định (Home)
};

// ================================================================
//  CharacterManager — singleton, quản lý CharacterData per user
//  Thread-safe (LoginServer + GameServer dùng chung)
// ================================================================
class CharacterManager
{
public:
    static CharacterManager& Get()
    {
        static CharacterManager inst;
        return inst;
    }

    // Đặt thư mục lưu file (mặc định "characters")
    void SetDirectory(const std::string& dir) { _dir = dir; }

    // Load data của username, nếu chưa có → tạo mặc định + save
    CharacterData LoadOrCreate(const std::string& username);

    // Lưu data
    void Save(const std::string& username, const CharacterData& data);

private:
    std::string _dir = "characters";

    std::string FilePath(const std::string& username) const;
    CharacterData Load(const std::string& username);
    bool Exists(const std::string& username) const;
};
