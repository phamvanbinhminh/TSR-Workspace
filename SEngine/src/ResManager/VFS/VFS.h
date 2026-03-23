#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../Pak/Pak.h"
#include "../../SExportEngineAPI.h"

// ============================================================
// VFS — Virtual File System (generic, không game-specific)
// ============================================================
// Cho phép mount nhiều .pak vào một namespace ảo.
// Tìm file theo thứ tự LIFO (mount sau → ưu tiên cao hơn).
//
// Usage:
//   VFS::Get().Mount("res/paks/res.pak", "res/");
//   auto bytes = VFS::Get().ReadFile("res/sprites/player.spr");
// ============================================================

class SENGINE_API VFS
{
public:
    // Singleton
    static VFS& Get();

    // Mount một .pak vào mountPoint ảo.
    // mountPoint = "" → file trong pak được tìm trực tiếp theo tên.
    // mountPoint = "res/" → tìm "res/foo.spr" trong pak dưới key "foo.spr".
    bool Mount(const std::string& pakPath,
               const std::string& mountPoint = "");

    // Unmount tất cả pak mount tại mountPoint đó
    void Unmount(const std::string& mountPoint);

    // Unmount tất cả
    void UnmountAll();

    // Đọc file từ VFS → trả về bytes (rỗng nếu không tìm thấy)
    std::vector<uint8_t> ReadFile(const std::string& virtualPath) const;

    // Kiểm tra file có tồn tại không
    bool Exists(const std::string& virtualPath) const;

    // Liệt kê tất cả mounted paks
    int  MountCount() const;

private:
    VFS() = default;

    struct MountEntry
    {
        std::string mountPoint; // e.g. "res/"
        Pak         pak;
    };

    // LIFO: cuối vector = ưu tiên cao nhất
    std::vector<MountEntry> _mounts;

    // Chuyển virtualPath → pak key dựa theo mountPoint
    // Ví dụ: virtualPath="res/sprites/a.spr", mountPoint="res/" → key="sprites/a.spr"
    static bool StripPrefix(const std::string& path,
                             const std::string& prefix,
                             std::string& outKey);
};
