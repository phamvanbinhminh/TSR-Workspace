#pragma message("Pak.cpp compiling")
#include "Pak.h"
#include <fstream>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

bool Pak::Create(const std::string& folder,
    const std::string& pakFile)
{
    std::vector<PakFile> fileList;

    // scan folder
    for (auto& p : fs::recursive_directory_iterator(folder))
    {
        if (!p.is_regular_file()) continue;

        std::ifstream f(p.path(), std::ios::binary);
        if (!f) continue;

        std::vector<uint8_t> data(
            (std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>()
        );

        PakFile pf;
        pf.name = fs::relative(p.path(), folder).string();
        pf.data = std::move(data);

        fileList.push_back(pf);
    }

    std::ofstream out(pakFile, std::ios::binary);
    if (!out) return false;

    PakHeader header{};
    memcpy(header.magic, "PAK0", 4);
    header.version = 1;
    header.fileCount = (uint32_t)fileList.size();

    header.tableOffset = sizeof(PakHeader);

    std::vector<PakFileEntry> entries(fileList.size());
    std::string stringTable;

    uint64_t currentOffset = 0;

    for (size_t i = 0; i < fileList.size(); i++)
    {
        auto& f = fileList[i];
        auto& e = entries[i];

        e.nameOffset = stringTable.size();
        stringTable += f.name;
        stringTable.push_back('\0');

        e.offset = currentOffset;
        e.size = f.data.size();
        e.compressedSize = f.data.size();

        e.compression = 0;
        e.encryption = 0;

        currentOffset += f.data.size();
    }

    header.stringTableOffset =
        header.tableOffset +
        sizeof(PakFileEntry) * entries.size();

    header.dataOffset =
        header.stringTableOffset +
        stringTable.size();

    // fix offsets
    for (auto& e : entries)
        e.offset += header.dataOffset;

    // write header
    out.write((char*)&header, sizeof(header));

    // write file table
    out.write((char*)entries.data(),
        entries.size() * sizeof(PakFileEntry));

    // write string table
    out.write(stringTable.data(), stringTable.size());

    // write file data
    for (auto& f : fileList)
        out.write((char*)f.data.data(), f.data.size());

    std::cout << "PAK created with "
        << fileList.size()
        << " files\n";

    return true;
}

bool Pak::Load(const std::string& pakFile)
{
    std::ifstream in(pakFile, std::ios::binary);
    if (!in) return false;

    PakHeader header;
    in.read((char*)&header, sizeof(header));

    if (strncmp(header.magic, "PAK0", 4) != 0)
        return false;

    // read file table
    std::vector<PakFileEntry> entries(header.fileCount);

    in.seekg(header.tableOffset);
    in.read((char*)entries.data(),
        entries.size() * sizeof(PakFileEntry));

    // read string table
    uint32_t stringSize =
        header.dataOffset - header.stringTableOffset;

    std::vector<char> stringTable(stringSize);

    in.seekg(header.stringTableOffset);
    in.read(stringTable.data(), stringSize);

    // read files
    for (auto& e : entries)
    {
        std::string name = &stringTable[e.nameOffset];

        std::vector<uint8_t> data(e.size);

        in.seekg(e.offset);
        in.read((char*)data.data(), e.size);

        files[name] = std::move(data);
    }

    return true;
}

const std::vector<uint8_t>& Pak::Get(const std::string& name) const
{
    static const std::vector<uint8_t> empty;
    auto it = files.find(name);
    return (it != files.end()) ? it->second : empty;
}
