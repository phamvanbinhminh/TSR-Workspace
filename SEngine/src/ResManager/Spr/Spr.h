#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ============================================================
// Compression flags (có thể OR với nhau)
// ============================================================
enum SprCompressionFlags : uint8_t
{
    SPR_COMPRESS_NONE        = 0x00,
    SPR_COMPRESS_RLE         = 0x01,  // Run-Length Encoding (per frame)
    SPR_COMPRESS_ZLIB        = 0x02,  // Deflate/zlib (per frame)
    SPR_COMPRESS_PALETTE     = 0x04,  // 8-bit palette (giảm từ RGBA8 xuống 256 màu)
    SPR_COMPRESS_ATLAS       = 0x08,  // Texture atlas (pack nhiều frame vào 1 sheet)
    SPR_COMPRESS_SKIP_ALPHA  = 0x10,  // Skip fully-transparent pixels (RLE aware)
};

// Encryption mode
enum SprEncryptionMode : uint8_t
{
    SPR_ENCRYPT_NONE  = 0x00,
    SPR_ENCRYPT_XOR   = 0x01,  // XOR với key 4 bytes (đơn giản, nhanh)
    SPR_ENCRYPT_AES128= 0x02,  // AES-128 ECB (reserved - future use)
};

// Cấu hình build – truyền vào SprWriter::BuildFromMemory / BuildFromFolder
struct SprBuildOptions
{
    uint8_t  compressionFlags = SPR_COMPRESS_NONE;
    uint8_t  encryptionMode   = SPR_ENCRYPT_NONE;
    uint8_t  xorKey[4]        = {0x53, 0x50, 0x52, 0x30}; // "SPR0"
    int      paletteSize      = 256;   // 2-256
    int      atlasMaxWidth    = 2048;  // max atlas sheet width
    int      atlasMaxHeight   = 2048;
};

// ============================================================
// SPR File Format (version 2 – compression + encryption)
// ============================================================
// Magic: "SPR2"
// SprHeader (68 bytes fixed)
//   animationTableOffset  → [animCount x 40-byte AnimEntry]
//   frameTableOffset      → [frameCount x sizeof(SprFrameEntry)]
//   metadataOffset        → [frameCount x sizeof(SprFrameMeta)]
//   paletteOffset         → [paletteSize x 4 bytes RGBA] (0 if no palette)
//   atlasOffset           → AtlasInfo + atlas pixel data  (0 if no atlas)
//   frameDataOffset       → per-frame compressed/encrypted data blocks
//
// Per-frame block layout:
//   uint32 compressedSize
//   uint32 originalSize
//   uint8  compressionFlags (copy of header flags for verification)
//   uint8  encryptionMode
//   uint8  padding[2]
//   [data bytes]
//
// AtlasInfo (when SPR_COMPRESS_ATLAS):
//   uint32 atlasWidth
//   uint32 atlasHeight
//   uint32 atlasCompressedSize  (zlib or raw depending on flags)
//   uint32 atlasOriginalSize
//   [atlas pixel data (RGBA8 or palette-indexed if combined)]
//   Per-frame UV rects stored in SprFrameEntry.atlasX/Y/W/H
// ============================================================

struct SprHeader
{
    char     magic[4];            // "SPR2"
    uint32_t version;             // 2

    uint16_t frameCount;
    uint16_t animationCount;

    uint16_t width;               // first frame width (reference)
    uint16_t height;

    int16_t  pivotX;
    int16_t  pivotY;

    uint8_t  pixelFormat;         // 0=RGBA8, 1=Palette8
    uint8_t  compression;         // SprCompressionFlags
    uint8_t  encryption;          // SprEncryptionMode
    uint8_t  reserved;

    uint32_t animationTableOffset;
    uint32_t frameTableOffset;
    uint32_t metadataOffset;
    uint32_t paletteOffset;       // 0 if no palette
    uint32_t atlasOffset;         // 0 if no atlas
    uint32_t frameDataOffset;

    uint32_t crc32;
};

struct SprFrameEntry
{
    uint32_t offset;     // byte offset from start of file to this frame's data block
    uint32_t size;       // total byte size of frame data block (header + data)

    uint16_t width;
    uint16_t height;

    int16_t  offsetX;
    int16_t  offsetY;

    uint16_t duration;   // ms

    // Atlas UV (in pixels within atlas sheet) – only used when SPR_COMPRESS_ATLAS
    uint16_t atlasX;
    uint16_t atlasY;
    uint16_t atlasW;
    uint16_t atlasH;
};

struct SprFrameMeta
{
    int16_t hitboxX;
    int16_t hitboxY;
    int16_t hitboxWidth;
    int16_t hitboxHeight;
    int16_t attackBoxX;
    int16_t attackBoxY;
    int16_t attackBoxWidth;
    int16_t attackBoxHeight;
};

// Per-frame data block header (written before each frame's bytes)
struct SprFrameBlockHeader
{
    uint32_t compressedSize;   // bytes following this header
    uint32_t originalSize;     // RGBA bytes before compression
    uint8_t  compressionFlags; // which techniques applied to THIS block
    uint8_t  encryptionMode;
    uint8_t  padding[2];
};

// Atlas info block (written at atlasOffset)
struct SprAtlasInfo
{
    uint32_t atlasWidth;
    uint32_t atlasHeight;
    uint32_t compressedSize;
    uint32_t originalSize;
    // followed by compressedSize bytes of atlas pixel data
};

// ============================================================
// Build-time structures
// ============================================================

struct SprFrameData
{
    int width;
    int height;
    int offsetX;
    int offsetY;
    int duration;          // ms
    SprFrameMeta metadata;
    const uint8_t* pixels; // RGBA8, width*height*4 bytes, caller keeps ownership
};

struct SprAnimData
{
    char     name[32];
    uint16_t startFrame;
    uint16_t frameCount;
    uint16_t fps;
    uint8_t  loop;
};

// ============================================================
// SprReader results
// ============================================================

struct SprLoadedFrame
{
    int width;
    int height;
    int offsetX;
    int offsetY;
    int duration;
    SprFrameMeta metadata;
    std::vector<uint8_t> pixels; // always RGBA8 after decompression/decrypt
};

struct SprLoadedAnim
{
    char     name[32];
    uint16_t startFrame;
    uint16_t frameCount;
    uint16_t fps;
    uint8_t  loop;
};

struct SprLoadedData
{
    SprHeader                header;
    std::vector<SprLoadedFrame> frames;
    std::vector<SprLoadedAnim>  animations;
    // Palette (if any) – 256 RGBA entries
    std::vector<uint8_t>     palette;   // size = paletteSize*4
};

// ============================================================
// SprReader
// ============================================================


#include "../../SExportEngineAPI.h"

class SENGINE_API SprReader
{
public:
    // Đọc file .spr – tự động giải nén + giải mã
    bool LoadFromFile(const std::string& filePath, SprLoadedData& outData,
                      const uint8_t xorKey[4] = nullptr);

    // Đọc từ buffer in-memory (dùng khi load từ PAK)
    bool LoadFromBuffer(const uint8_t* buffer, size_t bufferSize, SprLoadedData& outData,
                        const uint8_t xorKey[4] = nullptr);
};

// ============================================================
// SprWriter
// ============================================================

class SENGINE_API SprWriter
{
public:
    // Build từ folder ảnh trên disk (PNG/JPG/GIF)
    bool BuildFromFolder(
        const std::string& folder,
        const std::string& outputFile,
        int pivotX,
        int pivotY,
        int fps,
        const SprBuildOptions& opts = SprBuildOptions{}
    );

    // Build trực tiếp từ memory
    bool BuildFromMemory(
        const std::vector<SprFrameData>& frames,
        const std::vector<SprAnimData>&  animations,
        const std::string& outputFile,
        int pivotX,
        int pivotY,
        int fps,
        const SprBuildOptions& opts = SprBuildOptions{}
    );

    // Build ra buffer (để pack vào PAK)
    std::vector<uint8_t> BuildToBuffer(
        const std::vector<SprFrameData>& frames,
        const std::vector<SprAnimData>&  animations,
        int pivotX,
        int pivotY,
        int fps,
        const SprBuildOptions& opts = SprBuildOptions{}
    );
};
