#include "VFS.h"
#include "../../SExportEngineAPI.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <iterator>

// ── Singleton ────────────────────────────────────────────────────────────────

VFS& VFS::Get()
{
    static VFS instance;
    return instance;
}

// ── Helpers ──────────────────────────────────────────────────────────────────

bool VFS::StripPrefix(const std::string& path,
                      const std::string& prefix,
                      std::string&       outKey)
{
    if (prefix.empty())
    {
        outKey = path;
        return true;
    }
    if (path.size() >= prefix.size() &&
        path.compare(0, prefix.size(), prefix) == 0)
    {
        outKey = path.substr(prefix.size());
        return true;
    }
    return false;
}

// ── Mount / Unmount ──────────────────────────────────────────────────────────

bool VFS::Mount(const std::string& pakPath, const std::string& mountPoint)
{
    MountEntry entry;
    entry.mountPoint = mountPoint;

    // Normalise: đảm bảo mountPoint kết thúc bằng '/' nếu không rỗng
    if (!entry.mountPoint.empty() && entry.mountPoint.back() != '/')
        entry.mountPoint += '/';

    if (!entry.pak.Load(pakPath))
    {
        std::cerr << "[VFS] Failed to mount: " << pakPath << "\n";
        return false;
    }

    _mounts.push_back(std::move(entry));
    std::cout << "[VFS] Mounted: " << pakPath
              << " at \"" << _mounts.back().mountPoint << "\"\n";
    return true;
}

void VFS::Unmount(const std::string& mountPoint)
{
    std::string mp = mountPoint;
    if (!mp.empty() && mp.back() != '/') mp += '/';

    auto it = std::remove_if(_mounts.begin(), _mounts.end(),
        [&mp](const MountEntry& e) { return e.mountPoint == mp; });
    _mounts.erase(it, _mounts.end());
}

void VFS::UnmountAll()
{
    _mounts.clear();
}

int VFS::MountCount() const
{
    return (int)_mounts.size();
}

// ── ReadFile / Exists ────────────────────────────────────────────────────────

std::vector<uint8_t> VFS::ReadFile(const std::string& virtualPath) const
{
#ifdef SENGINE_DEBUG_DISK
    // ── Debug: ưu tiên đọc từ disk trước ────────────────────────────────────
    // virtualPath được dùng trực tiếp làm đường dẫn relative (cwd = thư mục exe)
    {
        std::ifstream diskFile(virtualPath, std::ios::binary);
        if (diskFile.is_open())
        {
            std::vector<uint8_t> bytes(
                (std::istreambuf_iterator<char>(diskFile)),
                std::istreambuf_iterator<char>());
            std::cout << "[VFS][DISK] " << virtualPath << " ("
                      << bytes.size() << " bytes)\n";
            return bytes;
        }
        // Không có trên disk → fallthrough sang PAK
    }
#endif

    // ── Production / Debug-PAK fallback: tìm trong mounted PAKs (LIFO) ──────
    for (int i = (int)_mounts.size() - 1; i >= 0; --i)
    {
        const MountEntry& entry = _mounts[i];
        std::string key;
        if (!StripPrefix(virtualPath, entry.mountPoint, key))
            continue;

        const auto& bytes = entry.pak.Get(key);
        if (!bytes.empty())
            return bytes;
    }

    std::cerr << "[VFS] File not found: " << virtualPath << "\n";
    return {};
}

bool VFS::Exists(const std::string& virtualPath) const
{
#ifdef SENGINE_DEBUG_DISK
    // Debug: kiểm tra disk trước
    {
        std::ifstream diskFile(virtualPath, std::ios::binary);
        if (diskFile.is_open())
            return true;
    }
#endif

    // PAK lookup
    for (int i = (int)_mounts.size() - 1; i >= 0; --i)
    {
        const MountEntry& entry = _mounts[i];
        std::string key;
        if (!StripPrefix(virtualPath, entry.mountPoint, key))
            continue;
        if (!entry.pak.Get(key).empty())
            return true;
    }
    return false;
}
