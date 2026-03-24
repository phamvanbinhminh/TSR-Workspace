#pragma once
// ═══════════════════════════════════════════════════════════
// AppConfig  –  lưu/đọc cấu hình toàn app vào file INI
//   File: resmanager.ini  (cạnh exe)
//
// Sử dụng:
//   AppConfig::Get().Load();          // khi khởi động
//   AppConfig::Get().Save();          // khi đóng app
//   AppConfig::Get().mapLastPath      // đường dẫn map cuối
// ═══════════════════════════════════════════════════════════
#include <string>
#include <vector>

struct AppConfig
{
    // ── Singleton ────────────────────────────────────────────
    static AppConfig& Get();

    // ── Persist ──────────────────────────────────────────────
    bool Load(const std::string& path = "");  // empty = cạnh exe
    bool Save(const std::string& path = "");

    // ── MapEditor ─────────────────────────────────────────────
    std::string mapLastPath;          // map folder đã load lần cuối
    std::string mapVfsRoot;           // VFS root directory
    int         mapExportType = 1;    // 0=S, 1=C
    std::vector<std::string> mapTilesetFolders;  // danh sách tileset folders

    // Path riêng khi load: có thể chọn chỉ load S hoặc C hoặc cả 2
    // 0 = auto (dùng mapLastPath), 1 = chỉ load server (S), 2 = chỉ load client (C)
    std::string mapLoadPathS;   // thư mục map version S (server)
    std::string mapLoadPathC;   // thư mục map version C (client)
    int         mapLoadMode = 0; // 0=auto, 1=S only, 2=C only

    // ── SprEditor ─────────────────────────────────────────────
    std::string sprLastLoadPath;   // file .spr đã load lần cuối
    std::string sprLastExportPath; // folder export .spr lần cuối
    std::string sprLastImportPath; // folder import images lần cuối

    // ── PakEditor ─────────────────────────────────────────────
    std::string pakLastPath;       // file .pak đã load lần cuối
    std::string pakLastExtractDir; // thư mục extract lần cuối
    std::string pakLastPackDir;    // thư mục pack lần cuối

    // ── Window state ─────────────────────────────────────────
    bool showMapEditor = false;
    bool showSprEditor = false;
    bool showPakEditor = false;

private:
    AppConfig() = default;
    std::string _iniPath;

    // INI helpers
    static std::string EscapeVal(const std::string& v);
    static std::string UnescapeVal(const std::string& v);
    static std::string GetExeDir();
};
