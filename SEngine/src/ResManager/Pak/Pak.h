#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct PakHeader
{
    char magic[4];          // "PAK0"
    uint32_t version;
    uint32_t fileCount;

    uint32_t tableOffset;
    uint32_t stringTableOffset;
    uint32_t dataOffset;

    uint32_t flags;
    uint32_t crc32;
};

struct PakFileEntry
{
    uint32_t nameOffset;

    uint64_t offset;
    uint64_t size;
    uint64_t compressedSize;

    uint32_t crc32;

    uint16_t compression;
    uint16_t encryption;

    uint32_t flags;
};

struct PakFile
{
    std::string name;
    std::vector<uint8_t> data;
};

class Pak
{
public:

    // pack folder -> pak file
    static bool Create(const std::string& folder,
        const std::string& pakFile);

    // load pak
    bool Load(const std::string& pakFile);

    // get file (const: dùng được trong VFS::ReadFile/Exists const)
    const std::vector<uint8_t>& Get(const std::string& name) const;

private:

    std::unordered_map<std::string, std::vector<uint8_t>> files;
};