#pragma once
#include "ResManager/VFS/VFS.h"
#include <filesystem>
#include <iostream>

// ============================================================
// ClientVFS — DaiGiangVoTan specific VFS initialization
// ============================================================
// Mount 3 pak chuẩn của client vào VFS singleton của SEngine.
// Nếu pak chưa có (dev mode) thì skip → VFS rỗng → code game
// sẽ fallback đọc thẳng từ disk (res/paks/<pak_name>/...).
//
// Cấu trúc thư mục:
//   res/paks/res.pak      hoặc  res/paks/res/<file>
//   res/paks/maps.pak     hoặc  res/paks/maps/<file>
//   res/paks/scripts.pak  hoặc  res/paks/scripts/<file>
//
// Gọi ClientVFS::Init() 1 lần duy nhất trong DGClientApp::Init()
// ============================================================

#include <fstream>
#include <sstream>
class ClientVFS
{
public:
    // Mount tất cả pak cần thiết.
    // basePath: thư mục chứa res/paks/ (mặc định = thư mục chạy exe)
    static void Init(const std::string& basePath = "")
    {
        std::string base = basePath;
        if (!base.empty() && base.back() != '/' && base.back() != '\\')
            base += '/';

        VFS& vfs = VFS::Get();

        std::ifstream file(base + "inis/paks.ini");
        if (!file.is_open())
        {
            // fallback (dev mode)
            TryMount(vfs, base + "res/paks/res.pak",     "res/");
            TryMount(vfs, base + "res/paks/maps.pak",    "maps/");
            TryMount(vfs, base + "res/paks/scripts.pak", "scripts/");
            return;
        }

        std::string line;
        while (std::getline(file, line))
        {
            // bỏ dòng rỗng / comment
            if (line.empty() || line[0] == '#')
                continue;

            std::string pakName;
            std::string mountPoint;

            size_t eq = line.find('=');
            if (eq != std::string::npos)
            {
                // dạng: file=mount
                pakName = line.substr(0, eq);
                mountPoint = line.substr(eq + 1);
            }
            else
            {
                // dạng: chỉ file → auto mount theo tên
                pakName = line;

                size_t dot = pakName.find('.');
                mountPoint = (dot != std::string::npos)
                    ? pakName.substr(0, dot) + "/"
                    : pakName + "/";
            }

            TryMount(vfs, base + "res/paks/" + pakName, mountPoint);
        }
    }

    // Unmount tất cả khi thoát game
    static void Shutdown()
    {
        VFS::Get().UnmountAll();
    }

private:
    static void TryMount(VFS& vfs,
                         const std::string& pakPath,
                         const std::string& mountPoint)
    {
        if (std::filesystem::exists(pakPath))
        {
            if (vfs.Mount(pakPath, mountPoint))
                std::cout << "[ClientVFS] Mounted: " << pakPath
                          << " -> " << mountPoint << "\n";
            else
                std::cerr << "[ClientVFS] Mount failed: " << pakPath << "\n";
        }
        else
        {
            std::cout << "[ClientVFS] Pak not found (disk mode): "
                      << pakPath << "\n";
        }
    }
};
