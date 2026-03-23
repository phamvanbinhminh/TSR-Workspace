1. PAK File Format

PAK là container file dùng để đóng gói toàn bộ tài nguyên game (sprite, texture, sound, map, script...).

File Layout
[PAK Header]
[File Table]
[String Table]
[File Data]
1.1 PAK Header
struct PakHeader
{
    char magic[4];      // "PAK0"
    uint32 version;     // format version

    uint32 fileCount;   // số lượng file

    uint32 tableOffset;       // offset tới File Table
    uint32 stringTableOffset; // offset tới String Table
    uint32 dataOffset;        // offset tới File Data

    uint32 flags;      // compression / encryption
    uint32 crc32;      // checksum toàn file pak
};
1.2 File Table
struct PakFileEntry
{
    uint32 nameOffset;     // offset tới tên file trong String Table

    uint64 offset;         // vị trí data trong pak
    uint64 size;           // size sau khi giải nén
    uint64 compressedSize; // size trước khi giải nén

    uint32 crc32;          // checksum file

    uint16 compression;    // 0 = none
                          // 1 = RLE
                          // 2 = ZLIB
                          // 3 = LZ4

    uint16 encryption;     // 0 = none
                          // 1 = XOR
                          // 2 = AES

    uint32 flags;
};
1.3 String Table

Danh sách tên file:

hero.spr
enemy.spr
ui/button.png
map/map01.dat
sound/hit.wav
1.4 File Data

Chứa dữ liệu binary của từng file.

[file1 data]
[file2 data]
[file3 data]