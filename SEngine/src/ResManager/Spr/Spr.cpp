#include "Spr.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cmath>
#include <unordered_map>

// ── STB image (define once here) ─────────────────────────────────────────────
#define STB_IMAGE_IMPLEMENTATION
#include "Renderer/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "Renderer/stb_image_write.h"

// ── zlib ─────────────────────────────────────────────────────────────────────
#include <zlib.h>

namespace fs = std::filesystem;

// ============================================================
// Internal helpers – forward declarations
// ============================================================
static uint32_t Crc32Compute(const uint8_t* data, size_t len);

// RLE encode/decode – forward declarations
static std::vector<uint8_t> RleDecode(const uint8_t* data, size_t dataSize, int pixelCount, bool skipAlpha);

// zlib wrap/unwrap
static std::vector<uint8_t> ZlibCompress(const uint8_t* data, size_t size, int level = Z_DEFAULT_COMPRESSION);
static std::vector<uint8_t> ZlibDecompress(const uint8_t* data, size_t size, size_t originalSize);

// XOR cipher
static void XorCrypt(uint8_t* data, size_t size, const uint8_t key[4]);

// Palette quantization (median-cut, max 256 colours)
struct Palette256
{
    uint8_t  colors[256][4]; // RGBA
    int      count;          // actual colours used (≤256)
};
static Palette256 QuantizePalette(const std::vector<const uint8_t*>& frames,
                                   const std::vector<int>& pixelCounts,
                                   int maxColors);
static std::vector<uint8_t> EncodeWithPalette(const uint8_t* rgba, int pixelCount,
                                               const Palette256& pal);
static std::vector<uint8_t> DecodePalette(const uint8_t* indexed, int pixelCount,
                                           const Palette256& pal);

// Texture atlas (simple row-packing shelf algorithm)
struct AtlasRect { int x, y, w, h, frameIdx; };
static bool PackAtlas(const std::vector<SprFrameData>& frames,
                      int maxW, int maxH,
                      std::vector<AtlasRect>& outRects,
                      int& outW, int& outH);

// ============================================================
// CRC-32 (simple table-based)
// ============================================================
static uint32_t Crc32Compute(const uint8_t* data, size_t len)
{
    static uint32_t table[256] = {};
    static bool tableInit = false;
    if (!tableInit)
    {
        for (uint32_t i = 0; i < 256; i++)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        tableInit = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ============================================================
// RLE Encode/Decode
// ============================================================
// Encoding scheme (pixel = 4 bytes RGBA):
//   Run of identical pixels : [count 1-254][R G B A]
//   Transparent-skip        : [0xFF][uint16_t LE count]  (skipAlpha only)
//   Literal run             : [0x00][count 1-255][pixels...]
// ─────────────────────────────────────────────────────────────
static std::vector<uint8_t> RleEncodeClean(const uint8_t* rgba, int pixelCount, bool skipAlpha)
{
    std::vector<uint8_t> out;
    out.reserve((size_t)pixelCount * 3);

    int i = 0;
    while (i < pixelCount)
    {
        const uint8_t* cur = rgba + (size_t)i * 4;

        // Transparent skip
        if (skipAlpha && cur[3] == 0)
        {
            int cnt = 0;
            while ((i + cnt) < pixelCount && (rgba + (size_t)(i + cnt) * 4)[3] == 0)
                cnt++;
            int remaining = cnt;
            while (remaining > 0)
            {
                int chunk = std::min(remaining, 65535);
                out.push_back(0xFF);
                out.push_back((uint8_t)(chunk & 0xFF));
                out.push_back((uint8_t)((chunk >> 8) & 0xFF));
                remaining -= chunk;
            }
            i += cnt;
            continue;
        }

        // Count run of identical pixels
        int runLen = 1;
        while ((i + runLen) < pixelCount && runLen < 254)
        {
            const uint8_t* nx = rgba + (size_t)(i + runLen) * 4;
            if (skipAlpha && nx[3] == 0) break;
            if (nx[0] == cur[0] && nx[1] == cur[1] &&
                nx[2] == cur[2] && nx[3] == cur[3])
                runLen++;
            else
                break;
        }

        if (runLen >= 3)
        {
            // Run token: [count 1-254][R G B A]
            out.push_back((uint8_t)runLen);
            out.push_back(cur[0]);
            out.push_back(cur[1]);
            out.push_back(cur[2]);
            out.push_back(cur[3]);
            i += runLen;
        }
        else
        {
            // Literal run: [0x00][count 1-255][pixels...]
            int litStart = i;
            int litCount = 0;
            while ((litStart + litCount) < pixelCount && litCount < 255)
            {
                const uint8_t* p = rgba + (size_t)(litStart + litCount) * 4;
                if (skipAlpha && p[3] == 0) break;
                // Check if a run of >=3 would start here
                if (litCount > 0)
                {
                    int ahead = 1;
                    while (ahead < 3 && (litStart + litCount + ahead) < pixelCount)
                    {
                        const uint8_t* q = rgba + (size_t)(litStart + litCount + ahead) * 4;
                        if (q[0] == p[0] && q[1] == p[1] &&
                            q[2] == p[2] && q[3] == p[3])
                            ahead++;
                        else
                            break;
                    }
                    if (ahead >= 3) break;
                }
                litCount++;
            }
            if (litCount == 0) litCount = 1; // always emit at least 1
            out.push_back(0x00);
            out.push_back((uint8_t)litCount);
            for (int k = 0; k < litCount; k++)
            {
                const uint8_t* p = rgba + (size_t)(litStart + k) * 4;
                out.push_back(p[0]);
                out.push_back(p[1]);
                out.push_back(p[2]);
                out.push_back(p[3]);
            }
            i = litStart + litCount;
        }
    }
    return out;
}

static std::vector<uint8_t> RleDecode(const uint8_t* data, size_t dataSize, int pixelCount, bool skipAlpha)
{
    std::vector<uint8_t> out((size_t)pixelCount * 4, 0);
    size_t src = 0;
    int    dst = 0; // pixel index

    while (dst < pixelCount && src < dataSize)
    {
        uint8_t token = data[src++];

        if (token == 0xFF)
        {
            // Transparent skip
            if (src + 1 >= dataSize) break;
            int cnt = (int)data[src] | ((int)data[src + 1] << 8);
            src += 2;
            // Pixels are already zero-initialized (transparent)
            dst += cnt;
        }
        else if (token == 0x00)
        {
            // Literal run
            if (src >= dataSize) break;
            int cnt = (int)data[src++];
            for (int k = 0; k < cnt && dst < pixelCount; k++, dst++)
            {
                if (src + 3 >= dataSize + 1) break;
                out[(size_t)dst * 4 + 0] = data[src++];
                out[(size_t)dst * 4 + 1] = data[src++];
                out[(size_t)dst * 4 + 2] = data[src++];
                out[(size_t)dst * 4 + 3] = data[src++];
            }
        }
        else
        {
            // Run of identical pixels
            int cnt = (int)token;
            if (src + 3 >= dataSize + 1) break;
            uint8_t r = data[src++];
            uint8_t g = data[src++];
            uint8_t b = data[src++];
            uint8_t a = data[src++];
            for (int k = 0; k < cnt && dst < pixelCount; k++, dst++)
            {
                out[(size_t)dst * 4 + 0] = r;
                out[(size_t)dst * 4 + 1] = g;
                out[(size_t)dst * 4 + 2] = b;
                out[(size_t)dst * 4 + 3] = a;
            }
        }
    }
    return out;
}

// ============================================================
// zlib helpers
// ============================================================
static std::vector<uint8_t> ZlibCompress(const uint8_t* data, size_t size, int level)
{
    if (!data || size == 0) return {};
    uLongf destLen = compressBound((uLong)size);
    std::vector<uint8_t> out(destLen);
    int ret = compress2(out.data(), &destLen, data, (uLong)size, level);
    if (ret != Z_OK)
    {
        std::cerr << "ZlibCompress failed: " << ret << "\n";
        return {};
    }
    out.resize(destLen);
    return out;
}

static std::vector<uint8_t> ZlibDecompress(const uint8_t* data, size_t size, size_t originalSize)
{
    if (!data || size == 0 || originalSize == 0) return {};
    std::vector<uint8_t> out(originalSize);
    uLongf destLen = (uLongf)originalSize;
    int ret = uncompress(out.data(), &destLen, data, (uLong)size);
    if (ret != Z_OK)
    {
        std::cerr << "ZlibDecompress failed: " << ret << "\n";
        return {};
    }
    out.resize(destLen);
    return out;
}

// ============================================================
// XOR cipher
// ============================================================
static void XorCrypt(uint8_t* data, size_t size, const uint8_t key[4])
{
    for (size_t i = 0; i < size; i++)
        data[i] ^= key[i & 3];
}

// ============================================================
// Palette quantization – simple median-cut (32-bin)
// ============================================================
struct ColorEntry { uint8_t r, g, b, a; };

static int ColorDistance(const uint8_t* a, const uint8_t* b)
{
    int dr = (int)a[0] - b[0];
    int dg = (int)a[1] - b[1];
    int db = (int)a[2] - b[2];
    int da = (int)a[3] - b[3];
    return dr*dr + dg*dg + db*db + da*da;
}

static int FindNearestColor(const Palette256& pal, const uint8_t* rgba)
{
    int best = 0, bestDist = INT_MAX;
    for (int i = 0; i < pal.count; i++)
    {
        int d = ColorDistance(rgba, pal.colors[i]);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

// Collect unique colors (up to maxColors) from all frames using a simple
// frequency-sort + truncation approach (fast, good enough for sprites)
static Palette256 QuantizePalette(const std::vector<const uint8_t*>& frames,
                                   const std::vector<int>& pixelCounts,
                                   int maxColors)
{
    // Collect all unique RGBA32 values and their counts
    std::unordered_map<uint32_t, uint32_t> freq;
    freq.reserve(4096);

    for (size_t f = 0; f < frames.size(); f++)
    {
        const uint8_t* px = frames[f];
        int n = pixelCounts[f];
        for (int i = 0; i < n; i++)
        {
            uint32_t key =  ((uint32_t)px[i*4+0])
                          | ((uint32_t)px[i*4+1] << 8)
                          | ((uint32_t)px[i*4+2] << 16)
                          | ((uint32_t)px[i*4+3] << 24);
            freq[key]++;
        }
    }

    // Sort by frequency descending, take top maxColors
    std::vector<std::pair<uint32_t, uint32_t>> sorted(freq.begin(), freq.end());
    std::sort(sorted.begin(), sorted.end(),
        [](const auto& a, const auto& b){ return a.second > b.second; });

    Palette256 pal{};
    pal.count = std::min((int)sorted.size(), maxColors);
    for (int i = 0; i < pal.count; i++)
    {
        uint32_t key = sorted[i].first;
        pal.colors[i][0] = (uint8_t)(key & 0xFF);
        pal.colors[i][1] = (uint8_t)((key >> 8) & 0xFF);
        pal.colors[i][2] = (uint8_t)((key >> 16) & 0xFF);
        pal.colors[i][3] = (uint8_t)((key >> 24) & 0xFF);
    }
    return pal;
}

static std::vector<uint8_t> EncodeWithPalette(const uint8_t* rgba, int pixelCount,
                                               const Palette256& pal)
{
    std::vector<uint8_t> out((size_t)pixelCount);
    for (int i = 0; i < pixelCount; i++)
        out[i] = (uint8_t)FindNearestColor(pal, rgba + (size_t)i * 4);
    return out;
}

static std::vector<uint8_t> DecodePalette(const uint8_t* indexed, int pixelCount,
                                           const Palette256& pal)
{
    std::vector<uint8_t> out((size_t)pixelCount * 4);
    for (int i = 0; i < pixelCount; i++)
    {
        int ci = indexed[i];
        if (ci >= pal.count) ci = 0;
        out[(size_t)i*4+0] = pal.colors[ci][0];
        out[(size_t)i*4+1] = pal.colors[ci][1];
        out[(size_t)i*4+2] = pal.colors[ci][2];
        out[(size_t)i*4+3] = pal.colors[ci][3];
    }
    return out;
}

// ============================================================
// Texture Atlas packing (shelf / row-based)
// ============================================================
static bool PackAtlas(const std::vector<SprFrameData>& frames,
                      int maxW, int maxH,
                      std::vector<AtlasRect>& outRects,
                      int& outW, int& outH)
{
    if (frames.empty()) return false;

    // Sort by height descending (standard shelf heuristic)
    std::vector<int> order(frames.size());
    for (int i = 0; i < (int)frames.size(); i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b){
        return frames[a].height > frames[b].height;
    });

    outRects.resize(frames.size());

    int curX = 0, curY = 0, shelfH = 0;
    outW = 0; outH = 0;

    for (int idx : order)
    {
        int fw = frames[idx].width;
        int fh = frames[idx].height;

        if (fw > maxW) { std::cerr << "Frame " << idx << " wider than atlas\n"; return false; }

        if (curX + fw > maxW)
        {
            // New shelf
            curY += shelfH;
            curX = 0;
            shelfH = 0;
        }
        if (curY + fh > maxH) { std::cerr << "Atlas too small\n"; return false; }

        outRects[idx] = { curX, curY, fw, fh, idx };
        curX  += fw;
        if (fh > shelfH) shelfH = fh;
        if (curX > outW)     outW = curX;
        if (curY + fh > outH) outH = curY + fh;
    }
    // Round up to next power-of-2 for better GPU compat
    auto nextPow2 = [](int v) -> int {
        v--; v|=v>>1; v|=v>>2; v|=v>>4; v|=v>>8; v|=v>>16; return v+1;
    };
    outW = std::min(nextPow2(outW), maxW);
    outH = std::min(nextPow2(outH), maxH);
    return true;
}

// ============================================================
// GIF loader (used by BuildFromFolder)
// ============================================================
struct FrameImage
{
    int width;
    int height;
    int duration; // ms (from GIF delays, or computed from fps)
    std::vector<uint8_t> pixels; // RGBA8
};

static bool LoadGifFrames(const std::string& path, int defaultFps,
                           std::vector<FrameImage>& images)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    int size = (int)ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<unsigned char> buffer((size_t)size);
    fread(buffer.data(), 1, size, f);
    fclose(f);

    int* delays = nullptr;
    int w, h, frames, comp;
    unsigned char* data = stbi_load_gif_from_memory(
        buffer.data(), size, &delays, &w, &h, &frames, &comp, 4);
    if (!data) return false;

    for (int i = 0; i < frames; i++)
    {
        FrameImage img;
        img.width  = w;
        img.height = h;
        img.duration = (delays && delays[i] > 0) ? delays[i] * 10
                       : (defaultFps > 0 ? 1000 / defaultFps : 33);
        unsigned char* ptr = data + (size_t)w * h * 4 * i;
        img.pixels.assign(ptr, ptr + (size_t)w * h * 4);
        images.push_back(std::move(img));
    }

    stbi_image_free(data);
    if (delays) free(delays);
    return true;
}

// ============================================================
// Core build function – applies compression / encryption
// ============================================================
static std::vector<uint8_t> BuildSprCore(
    const std::vector<SprFrameData>& frames,
    const std::vector<SprAnimData>&  animations,
    int pivotX, int pivotY, int fps,
    const SprBuildOptions& opts)
{
    if (frames.empty()) return {};

    const bool useRle      = (opts.compressionFlags & SPR_COMPRESS_RLE)        != 0;
    const bool useZlib     = (opts.compressionFlags & SPR_COMPRESS_ZLIB)       != 0;
    const bool usePalette  = (opts.compressionFlags & SPR_COMPRESS_PALETTE)    != 0;
    const bool useAtlas    = (opts.compressionFlags & SPR_COMPRESS_ATLAS)      != 0;
    const bool useSkipAlpha= (opts.compressionFlags & SPR_COMPRESS_SKIP_ALPHA) != 0;
    const bool useXor      = (opts.encryptionMode   == SPR_ENCRYPT_XOR);

    constexpr uint32_t ANIM_ENTRY_SIZE = 40;
    uint16_t animCount = (uint16_t)(animations.empty() ? 1 : animations.size());

    // ── Step 1: Build palette (if requested) ─────────────────
    Palette256 pal{};
    if (usePalette)
    {
        std::vector<const uint8_t*> ptrs;
        std::vector<int>            pxCnts;
        for (auto& fd : frames)
        {
            ptrs.push_back(fd.pixels);
            pxCnts.push_back(fd.width * fd.height);
        }
        pal = QuantizePalette(ptrs, pxCnts, std::clamp(opts.paletteSize, 2, 256));
    }

    // ── Step 2: Build atlas (if requested) ───────────────────
    std::vector<AtlasRect> atlasRects;
    int atlasW = 0, atlasH = 0;
    std::vector<uint8_t> atlasPixels;
    std::vector<uint8_t> atlasCompressed;
    bool atlasPackOk = false;

    if (useAtlas)
    {
        atlasPackOk = PackAtlas(frames, opts.atlasMaxWidth, opts.atlasMaxHeight,
                                atlasRects, atlasW, atlasH);
        if (!atlasPackOk)
        {
            std::cerr << "BuildSprCore: atlas packing failed, falling back to per-frame\n";
        }
        else
        {
            // Composite all frames into atlas RGBA
            atlasPixels.assign((size_t)atlasW * atlasH * 4, 0);
            for (auto& fd : frames)
            {
                int fi = (int)(&fd - frames.data());
                const AtlasRect& r = atlasRects[fi];
                for (int row = 0; row < fd.height; row++)
                {
                    const uint8_t* src = fd.pixels + (size_t)row * fd.width * 4;
                    uint8_t*       dst = atlasPixels.data()
                                       + ((size_t)(r.y + row) * atlasW + r.x) * 4;
                    memcpy(dst, src, (size_t)fd.width * 4);
                }
            }
            // Optionally compress atlas data
            if (useZlib)
                atlasCompressed = ZlibCompress(atlasPixels.data(), atlasPixels.size());
            else
                atlasCompressed = atlasPixels;
        }
    }

    bool atlasReady = useAtlas && !atlasPixels.empty();

    // ── Step 3: Per-frame compression ────────────────────────
    // Each frame produces a "data block" = SprFrameBlockHeader + bytes
    struct FrameBlock
    {
        SprFrameBlockHeader hdr;
        std::vector<uint8_t> data;
    };
    std::vector<FrameBlock> blocks(frames.size());

    for (size_t fi = 0; fi < frames.size(); fi++)
    {
        const SprFrameData& fd = frames[fi];
        int pixelCount = fd.width * fd.height;
        uint32_t rawSize = (uint32_t)pixelCount * 4;

        std::vector<uint8_t> work;

        if (atlasReady)
        {
            // In atlas mode each frame block stores NO pixel data
            // (atlas sheet is stored separately). Block is empty.
            blocks[fi].hdr = {};
            blocks[fi].hdr.originalSize     = rawSize;
            blocks[fi].hdr.compressedSize   = 0;
            blocks[fi].hdr.compressionFlags = opts.compressionFlags;
            blocks[fi].hdr.encryptionMode   = opts.encryptionMode;
            continue;
        }

        if (usePalette)
        {
            // RGBA → palette-indexed (1 byte per pixel)
            work = EncodeWithPalette(fd.pixels, pixelCount, pal);
            rawSize = (uint32_t)pixelCount; // 1 byte/pixel
        }
        else
        {
            work.assign(fd.pixels, fd.pixels + (size_t)pixelCount * 4);
        }

        if (useRle)
        {
            std::vector<uint8_t> rleWork;
            if (usePalette)
            {
                // Byte-level RLE for palette-indexed data (1 byte per pixel)
                int i = 0;
                int n = (int)work.size();
                while (i < n)
                {
                    uint8_t val = work[i];

                    // Transparent-skip: palette index 0 treated as transparent
                    if (useSkipAlpha && val == 0)
                    {
                        int skipCnt = 0;
                        while ((i + skipCnt) < n && work[i + skipCnt] == 0)
                            skipCnt++;
                        int rem = skipCnt;
                        while (rem > 0)
                        {
                            int c = std::min(rem, 65535);
                            rleWork.push_back(0xFF);
                            rleWork.push_back((uint8_t)(c & 0xFF));
                            rleWork.push_back((uint8_t)((c >> 8) & 0xFF));
                            rem -= c;
                        }
                        i += skipCnt;
                        continue;
                    }

                    // Count run of identical bytes
                    int runLen = 1;
                    while ((i + runLen) < n && runLen < 254 && work[i + runLen] == val)
                        runLen++;

                    if (runLen >= 3)
                    {
                        rleWork.push_back((uint8_t)runLen);
                        rleWork.push_back(val);
                        i += runLen;
                    }
                    else
                    {
                        int litStart = i;
                        int litCnt = 0;
                        while ((litStart + litCnt) < n && litCnt < 255)
                        {
                            uint8_t lv = work[litStart + litCnt];
                            if (useSkipAlpha && lv == 0) break;
                            if (litCnt > 0)
                            {
                                int ahead = 1;
                                while (ahead < 3 && (litStart + litCnt + ahead) < n &&
                                       work[litStart + litCnt + ahead] == lv)
                                    ahead++;
                                if (ahead >= 3) break;
                            }
                            litCnt++;
                        }
                        if (litCnt == 0) litCnt = 1;
                        rleWork.push_back(0x00);
                        rleWork.push_back((uint8_t)litCnt);
                        for (int k = 0; k < litCnt; k++)
                            rleWork.push_back(work[litStart + k]);
                        i = litStart + litCnt;
                    }
                }
                work = std::move(rleWork);
            }
            else
            {
                // RGBA pixel-level RLE
                work = RleEncodeClean(fd.pixels, pixelCount, useSkipAlpha);
            }
        }

        if (useZlib)
        {
            auto compressed = ZlibCompress(work.data(), work.size());
            if (!compressed.empty()) work = std::move(compressed);
        }

        if (useXor)
            XorCrypt(work.data(), work.size(), opts.xorKey);

        FrameBlock& blk = blocks[fi];
        blk.hdr.compressedSize   = (uint32_t)work.size();
        blk.hdr.originalSize     = rawSize;
        blk.hdr.compressionFlags = opts.compressionFlags;
        blk.hdr.encryptionMode   = opts.encryptionMode;
        blk.data                 = std::move(work);
    }

    // ── Step 4: Compute layout offsets ───────────────────────
    SprHeader header{};
    memcpy(header.magic, "SPR2", 4);
    header.version        = 2;
    header.frameCount     = (uint16_t)frames.size();
    header.animationCount = animCount;
    header.width          = (uint16_t)frames[0].width;
    header.height         = (uint16_t)frames[0].height;
    header.pivotX         = (int16_t)pivotX;
    header.pivotY         = (int16_t)pivotY;
    header.pixelFormat    = usePalette ? 1 : 0;
    header.compression    = opts.compressionFlags;
    header.encryption     = opts.encryptionMode;

    header.animationTableOffset = sizeof(SprHeader);
    header.frameTableOffset     = header.animationTableOffset + ANIM_ENTRY_SIZE * animCount;
    header.metadataOffset       = header.frameTableOffset
                                  + (uint32_t)(sizeof(SprFrameEntry) * frames.size());

    // Palette block
    uint32_t paletteBytes = usePalette ? (uint32_t)(pal.count * 4) : 0;
    header.paletteOffset  = usePalette
                            ? (header.metadataOffset + (uint32_t)(sizeof(SprFrameMeta) * frames.size()))
                            : 0;

    // Atlas block
    uint32_t atlasBlockBytes = 0;
    if (atlasReady)
    {
        atlasBlockBytes = (uint32_t)(sizeof(SprAtlasInfo) + atlasCompressed.size());
        header.atlasOffset = (header.paletteOffset > 0 ? header.paletteOffset + paletteBytes
                              : header.metadataOffset + (uint32_t)(sizeof(SprFrameMeta) * frames.size()));
    }

    uint32_t frameDataStart = (header.paletteOffset > 0 ? header.paletteOffset + paletteBytes
                               : header.metadataOffset + (uint32_t)(sizeof(SprFrameMeta) * frames.size()));
    if (atlasReady) frameDataStart = header.atlasOffset + atlasBlockBytes;
    header.frameDataOffset = frameDataStart;

    // Frame table entries (with final offsets)
    std::vector<SprFrameEntry> frameEntries(frames.size());
    uint32_t dataOff = header.frameDataOffset;
    for (size_t fi = 0; fi < frames.size(); fi++)
    {
        SprFrameEntry& fe = frameEntries[fi];
        fe.width    = (uint16_t)frames[fi].width;
        fe.height   = (uint16_t)frames[fi].height;
        fe.offsetX  = (int16_t)frames[fi].offsetX;
        fe.offsetY  = (int16_t)frames[fi].offsetY;
        fe.duration = (uint16_t)(frames[fi].duration > 0 ? frames[fi].duration
                                 : (fps > 0 ? 1000/fps : 33));
        fe.offset   = dataOff;
        uint32_t blkSize = (uint32_t)(sizeof(SprFrameBlockHeader) + blocks[fi].data.size());
        fe.size     = blkSize;
        dataOff    += blkSize;

        if (atlasReady)
        {
            fe.atlasX = (uint16_t)atlasRects[fi].x;
            fe.atlasY = (uint16_t)atlasRects[fi].y;
            fe.atlasW = (uint16_t)atlasRects[fi].w;
            fe.atlasH = (uint16_t)atlasRects[fi].h;
        }
    }

    // ── Step 5: Assemble binary buffer ───────────────────────
    std::vector<uint8_t> buf;
    buf.reserve(dataOff + 256);

    auto wb = [&](const void* d, size_t n) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(d);
        buf.insert(buf.end(), p, p + n);
    };

    // Placeholder header (will patch CRC later)
    size_t headerOffset = buf.size();
    wb(&header, sizeof(header));

    // Animation table
    struct AnimEntry {
        char     name[32];
        uint16_t startFrame;
        uint16_t frameCount;
        uint16_t fps;
        uint8_t  loop;
        uint8_t  reserved;
    };
    static_assert(sizeof(AnimEntry) == ANIM_ENTRY_SIZE, "AnimEntry size mismatch");

    if (animations.empty())
    {
        AnimEntry ae{};
        strcpy(ae.name, "default");
        ae.startFrame = 0;
        ae.frameCount = (uint16_t)frames.size();
        ae.fps        = (uint16_t)(fps > 0 ? fps : 30);
        ae.loop       = 1;
        wb(&ae, sizeof(ae));
    }
    else
    {
        for (auto& ad : animations)
        {
            AnimEntry ae{};
            strncpy(ae.name, ad.name, 31);
            ae.name[31]   = '\0';
            ae.startFrame = ad.startFrame;
            ae.frameCount = ad.frameCount;
            ae.fps        = ad.fps > 0 ? ad.fps : (uint16_t)(fps > 0 ? fps : 30);
            ae.loop       = ad.loop;
            wb(&ae, sizeof(ae));
        }
    }

    // Frame table
    wb(frameEntries.data(), frameEntries.size() * sizeof(SprFrameEntry));

    // Metadata
    for (auto& fd : frames) wb(&fd.metadata, sizeof(SprFrameMeta));

    // Palette
    if (usePalette)
    {
        for (int i = 0; i < pal.count; i++) wb(pal.colors[i], 4);
    }

    // Atlas info + pixels
    if (atlasReady)
    {
        SprAtlasInfo ai{};
        ai.atlasWidth      = (uint32_t)atlasW;
        ai.atlasHeight     = (uint32_t)atlasH;
        ai.compressedSize  = (uint32_t)atlasCompressed.size();
        ai.originalSize    = (uint32_t)atlasPixels.size();
        wb(&ai, sizeof(ai));
        wb(atlasCompressed.data(), atlasCompressed.size());
    }

    // Per-frame blocks
    for (auto& blk : blocks)
    {
        wb(&blk.hdr, sizeof(blk.hdr));
        if (!blk.data.empty()) wb(blk.data.data(), blk.data.size());
    }

    // Patch CRC (over everything after the header's crc32 field)
    // CRC covers bytes from magic through end-of-file, with crc32 field zeroed
    {
        uint32_t crc = Crc32Compute(buf.data(), buf.size());
        memcpy(buf.data() + headerOffset + offsetof(SprHeader, crc32), &crc, 4);
    }

    return buf;
}

// ============================================================
// SprReader – parse SPR binary (version 1 and 2)
// ============================================================
static bool ParseSprV1(const uint8_t* buf, size_t bufSize, SprLoadedData& out);
static bool ParseSprV2(const uint8_t* buf, size_t bufSize, SprLoadedData& out, const uint8_t xorKey[4]);

static bool ParseSprBuffer(const uint8_t* buf, size_t bufSize, SprLoadedData& out,
                            const uint8_t xorKey[4])
{
    if (!buf || bufSize < sizeof(SprHeader)) return false;

    if (memcmp(buf, "SPR2", 4) == 0)
        return ParseSprV2(buf, bufSize, out, xorKey);
    if (memcmp(buf, "SPR0", 4) == 0)
        return ParseSprV1(buf, bufSize, out);

    std::cerr << "SprReader: unknown magic\n";
    return false;
}

// ── Version 1 (legacy, no compression) ───────────────────────────────────────
static bool ParseSprV1(const uint8_t* buf, size_t bufSize, SprLoadedData& out)
{
    // Version 1 SprHeader layout (from old Spr.h):
    //  char magic[4], uint32 version, uint16 frameCount, uint16 animCount,
    //  uint16 width, uint16 height, int16 pivotX, int16 pivotY,
    //  uint8 pixelFormat, uint8 compression, uint8 flags, uint8 reserved,
    //  uint32 animTableOff, uint32 frameTableOff, uint32 metaOff,
    //  uint32 paletteOff, uint32 frameDataOff, uint32 crc32  (= 56 bytes)
    struct V1Header {
        char     magic[4];
        uint32_t version;
        uint16_t frameCount, animationCount;
        uint16_t width, height;
        int16_t  pivotX, pivotY;
        uint8_t  pixelFormat, compression, flags, reserved;
        uint32_t animationTableOffset, frameTableOffset, metadataOffset;
        uint32_t paletteOffset, frameDataOffset, crc32;
    };
    // Old SprFrameEntry (no atlasX/Y/W/H):
    struct V1FrameEntry {
        uint32_t offset, size;
        uint16_t width, height;
        int16_t  offsetX, offsetY;
        uint16_t duration;
    };
    constexpr uint32_t ANIM_ENTRY_SIZE = 40;
    struct AnimEntry {
        char name[32]; uint16_t startFrame, frameCount, fps; uint8_t loop, reserved;
    };

    if (bufSize < sizeof(V1Header)) return false;
    V1Header h;
    memcpy(&h, buf, sizeof(h));

    // Copy to new SprHeader (fields map 1:1 except atlasOffset missing)
    memset(&out.header, 0, sizeof(out.header));
    memcpy(out.header.magic, h.magic, 4);
    out.header.version             = h.version;
    out.header.frameCount          = h.frameCount;
    out.header.animationCount      = h.animationCount;
    out.header.width               = h.width;
    out.header.height              = h.height;
    out.header.pivotX              = h.pivotX;
    out.header.pivotY              = h.pivotY;
    out.header.pixelFormat         = h.pixelFormat;
    out.header.compression         = h.compression;
    out.header.animationTableOffset= h.animationTableOffset;
    out.header.frameTableOffset    = h.frameTableOffset;
    out.header.metadataOffset      = h.metadataOffset;
    out.header.paletteOffset       = h.paletteOffset;
    out.header.frameDataOffset     = h.frameDataOffset;
    out.header.crc32               = h.crc32;

    // Animations
    out.animations.resize(h.animationCount);
    for (uint16_t i = 0; i < h.animationCount; i++)
    {
        size_t off = h.animationTableOffset + (size_t)i * ANIM_ENTRY_SIZE;
        if (off + ANIM_ENTRY_SIZE > bufSize) return false;
        AnimEntry ae; memcpy(&ae, buf + off, sizeof(ae));
        memcpy(out.animations[i].name, ae.name, 32);
        out.animations[i].startFrame = ae.startFrame;
        out.animations[i].frameCount = ae.frameCount;
        out.animations[i].fps        = ae.fps;
        out.animations[i].loop       = ae.loop;
    }

    // Frames
    out.frames.resize(h.frameCount);
    for (uint16_t i = 0; i < h.frameCount; i++)
    {
        size_t off = h.frameTableOffset + (size_t)i * sizeof(V1FrameEntry);
        if (off + sizeof(V1FrameEntry) > bufSize) return false;
        V1FrameEntry fe; memcpy(&fe, buf + off, sizeof(fe));
        out.frames[i].width    = fe.width;
        out.frames[i].height   = fe.height;
        out.frames[i].offsetX  = fe.offsetX;
        out.frames[i].offsetY  = fe.offsetY;
        out.frames[i].duration = fe.duration;
    }

    // Metadata
    for (uint16_t i = 0; i < h.frameCount; i++)
    {
        size_t off = h.metadataOffset + (size_t)i * sizeof(SprFrameMeta);
        if (off + sizeof(SprFrameMeta) <= bufSize)
            memcpy(&out.frames[i].metadata, buf + off, sizeof(SprFrameMeta));
    }

    // Pixels (raw RGBA, no compression for v1)
    for (uint16_t i = 0; i < h.frameCount; i++)
    {
        size_t off = h.frameTableOffset + (size_t)i * sizeof(V1FrameEntry);
        V1FrameEntry fe; memcpy(&fe, buf + off, sizeof(fe));
        if (fe.size == 0 || fe.offset + fe.size > bufSize) continue;
        out.frames[i].pixels.assign(buf + fe.offset, buf + fe.offset + fe.size);
    }
    return true;
}

// ── Version 2 (full compression + encryption) ────────────────────────────────
static bool ParseSprV2(const uint8_t* buf, size_t bufSize, SprLoadedData& out,
                       const uint8_t xorKey[4])
{
    if (bufSize < sizeof(SprHeader)) return false;
    memcpy(&out.header, buf, sizeof(SprHeader));
    const SprHeader& h = out.header;

    if (memcmp(h.magic, "SPR2", 4) != 0) return false;

    constexpr uint32_t ANIM_ENTRY_SIZE = 40;
    struct AnimEntry {
        char name[32]; uint16_t startFrame, frameCount, fps; uint8_t loop, reserved;
    };

    const bool usePalette  = (h.compression & SPR_COMPRESS_PALETTE)    != 0;
    const bool useRle      = (h.compression & SPR_COMPRESS_RLE)        != 0;
    const bool useZlib     = (h.compression & SPR_COMPRESS_ZLIB)       != 0;
    const bool useSkip     = (h.compression & SPR_COMPRESS_SKIP_ALPHA) != 0;
    const bool useAtlas    = (h.compression & SPR_COMPRESS_ATLAS)      != 0;
    const bool useXor      = (h.encryption  == SPR_ENCRYPT_XOR);

    static const uint8_t defaultKey[4] = {0x53, 0x50, 0x52, 0x30};
    const uint8_t* key = (xorKey != nullptr) ? xorKey : defaultKey;

    // ── Animations ──────────────────────────────────────────
    out.animations.resize(h.animationCount);
    for (uint16_t i = 0; i < h.animationCount; i++)
    {
        size_t off = h.animationTableOffset + (size_t)i * ANIM_ENTRY_SIZE;
        if (off + ANIM_ENTRY_SIZE > bufSize) return false;
        AnimEntry ae; memcpy(&ae, buf + off, sizeof(ae));
        memcpy(out.animations[i].name, ae.name, 32);
        out.animations[i].startFrame = ae.startFrame;
        out.animations[i].frameCount = ae.frameCount;
        out.animations[i].fps        = ae.fps;
        out.animations[i].loop       = ae.loop;
    }

    // ── Frame table ─────────────────────────────────────────
    out.frames.resize(h.frameCount);
    std::vector<SprFrameEntry> frameEntries(h.frameCount);
    for (uint16_t i = 0; i < h.frameCount; i++)
    {
        size_t off = h.frameTableOffset + (size_t)i * sizeof(SprFrameEntry);
        if (off + sizeof(SprFrameEntry) > bufSize) return false;
        memcpy(&frameEntries[i], buf + off, sizeof(SprFrameEntry));
        out.frames[i].width    = frameEntries[i].width;
        out.frames[i].height   = frameEntries[i].height;
        out.frames[i].offsetX  = frameEntries[i].offsetX;
        out.frames[i].offsetY  = frameEntries[i].offsetY;
        out.frames[i].duration = frameEntries[i].duration;
    }

    // ── Metadata ────────────────────────────────────────────
    for (uint16_t i = 0; i < h.frameCount; i++)
    {
        size_t off = h.metadataOffset + (size_t)i * sizeof(SprFrameMeta);
        if (off + sizeof(SprFrameMeta) <= bufSize)
            memcpy(&out.frames[i].metadata, buf + off, sizeof(SprFrameMeta));
    }

    // ── Palette ─────────────────────────────────────────────
    Palette256 pal{};
    if (usePalette && h.paletteOffset > 0)
    {
        // Count: number of colors = (frameDataOffset - paletteOffset) / 4
        // Actually we need to reconstruct – use paletteSize from opts (not stored in header v2).
        // Store count as: until next section (atlas or frameData).
        uint32_t nextOff = (h.atlasOffset > 0) ? h.atlasOffset : h.frameDataOffset;
        uint32_t palBytes = nextOff - h.paletteOffset;
        pal.count = std::min((int)(palBytes / 4), 256);
        out.palette.assign(buf + h.paletteOffset, buf + h.paletteOffset + pal.count * 4);
        for (int i = 0; i < pal.count; i++)
            memcpy(pal.colors[i], out.palette.data() + (size_t)i * 4, 4);
    }

    // ── Atlas ───────────────────────────────────────────────
    std::vector<uint8_t> atlasPixels;
    int atlasW = 0, atlasH = 0;
    if (useAtlas && h.atlasOffset > 0)
    {
        size_t off = h.atlasOffset;
        if (off + sizeof(SprAtlasInfo) > bufSize) return false;
        SprAtlasInfo ai; memcpy(&ai, buf + off, sizeof(ai));
        off += sizeof(ai);
        atlasW = (int)ai.atlasWidth;
        atlasH = (int)ai.atlasHeight;

        if (off + ai.compressedSize > bufSize) return false;
        if (useZlib && ai.compressedSize != ai.originalSize)
            atlasPixels = ZlibDecompress(buf + off, ai.compressedSize, ai.originalSize);
        else
            atlasPixels.assign(buf + off, buf + off + ai.compressedSize);
    }

    // ── Per-frame data ──────────────────────────────────────
    for (uint16_t i = 0; i < h.frameCount; i++)
    {
        const SprFrameEntry& fe = frameEntries[i];
        int pixelCount = out.frames[i].width * out.frames[i].height;

        if (useAtlas && !atlasPixels.empty())
        {
            // Extract sub-rect from atlas
            int ax = fe.atlasX, ay = fe.atlasY, aw = fe.atlasW, ah = fe.atlasH;
            out.frames[i].pixels.resize((size_t)aw * ah * 4, 0);
            for (int row = 0; row < ah; row++)
            {
                const uint8_t* src = atlasPixels.data() + ((size_t)(ay + row) * atlasW + ax) * 4;
                uint8_t*       dst = out.frames[i].pixels.data() + (size_t)row * aw * 4;
                memcpy(dst, src, (size_t)aw * 4);
            }
            continue;
        }

        if (fe.offset + fe.size > bufSize) continue;
        if (fe.size < sizeof(SprFrameBlockHeader)) continue;

        SprFrameBlockHeader blkHdr;
        memcpy(&blkHdr, buf + fe.offset, sizeof(blkHdr));

        size_t dataStart = fe.offset + sizeof(SprFrameBlockHeader);
        size_t dataLen   = blkHdr.compressedSize;
        if (dataStart + dataLen > bufSize) continue;

        // Copy compressed data
        std::vector<uint8_t> work(buf + dataStart, buf + dataStart + dataLen);

        // 1. Decrypt
        if (useXor) XorCrypt(work.data(), work.size(), key);

        // 2. ZLIB decompress
        if (useZlib && !work.empty())
        {
            uint32_t expectedOriginal = blkHdr.originalSize;
            auto dec = ZlibDecompress(work.data(), work.size(), expectedOriginal);
            if (!dec.empty()) work = std::move(dec);
        }

        // 3. RLE decode
        if (useRle && !work.empty())
        {
            auto dec = RleDecode(work.data(), work.size(), pixelCount, useSkip);
            if (!dec.empty()) work = std::move(dec);
        }

        // 4. Palette decode
        if (usePalette && !work.empty() && pal.count > 0)
        {
            // If RLE was applied, work should already be pixelCount bytes
            auto dec = DecodePalette(work.data(),
                                     (int)std::min((size_t)pixelCount, work.size()),
                                     pal);
            if (!dec.empty()) work = std::move(dec);
        }

        out.frames[i].pixels = std::move(work);
    }

    return true;
}

// ============================================================
// SprReader – public API
// ============================================================
bool SprReader::LoadFromFile(const std::string& filePath, SprLoadedData& outData,
                              const uint8_t xorKey[4])
{
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file) { std::cerr << "SprReader: cannot open: " << filePath << "\n"; return false; }

    size_t sz = (size_t)file.tellg();
    file.seekg(0);
    std::vector<uint8_t> buf(sz);
    if (!file.read(reinterpret_cast<char*>(buf.data()), sz))
    { std::cerr << "SprReader: read error\n"; return false; }

    return ParseSprBuffer(buf.data(), buf.size(), outData, xorKey);
}

bool SprReader::LoadFromBuffer(const uint8_t* buffer, size_t bufferSize,
                                SprLoadedData& outData, const uint8_t xorKey[4])
{
    return ParseSprBuffer(buffer, bufferSize, outData, xorKey);
}

// ============================================================
// SprWriter – public API
// ============================================================
bool SprWriter::BuildFromFolder(
    const std::string& folder,
    const std::string& outputFile,
    int pivotX, int pivotY, int fps,
    const SprBuildOptions& opts)
{
    // Collect image files
    std::vector<fs::path> paths;
    for (auto& entry : fs::directory_iterator(folder))
    {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif")
            paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());

    std::vector<FrameImage> images;
    for (auto& p : paths)
    {
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".gif")
        {
            LoadGifFrames(p.string(), fps, images);
        }
        else
        {
            int w, h, ch;
            unsigned char* data = stbi_load(p.string().c_str(), &w, &h, &ch, 4);
            if (!data) { std::cerr << "stbi_load failed: " << p << "\n"; continue; }
            FrameImage img;
            img.width    = w; img.height = h;
            img.duration = fps > 0 ? 1000 / fps : 33;
            img.pixels.assign(data, data + (size_t)w * h * 4);
            stbi_image_free(data);
            images.push_back(std::move(img));
        }
    }

    if (images.empty()) { std::cerr << "BuildFromFolder: no images found in " << folder << "\n"; return false; }

    // Convert to SprFrameData
    std::vector<SprFrameData> frameData;
    for (auto& img : images)
    {
        SprFrameData fd{};
        fd.width    = img.width;
        fd.height   = img.height;
        fd.duration = img.duration;
        fd.pixels   = img.pixels.data();
        frameData.push_back(fd);
    }

    auto buf = BuildSprCore(frameData, {}, pivotX, pivotY, fps, opts);
    if (buf.empty()) return false;

    std::ofstream file(outputFile, std::ios::binary);
    if (!file) { std::cerr << "Cannot open output: " << outputFile << "\n"; return false; }
    file.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    std::cout << "SPR built: " << outputFile << "  (" << buf.size() << " bytes)\n";
    return true;
}

bool SprWriter::BuildFromMemory(
    const std::vector<SprFrameData>& frames,
    const std::vector<SprAnimData>&  animations,
    const std::string& outputFile,
    int pivotX, int pivotY, int fps,
    const SprBuildOptions& opts)
{
    auto buf = BuildToBuffer(frames, animations, pivotX, pivotY, fps, opts);
    if (buf.empty()) return false;

    std::ofstream file(outputFile, std::ios::binary);
    if (!file) { std::cerr << "Cannot open output: " << outputFile << "\n"; return false; }
    file.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    std::cout << "SPR built (memory): " << outputFile << "  (" << buf.size() << " bytes)\n";
    return true;
}

std::vector<uint8_t> SprWriter::BuildToBuffer(
    const std::vector<SprFrameData>& frames,
    const std::vector<SprAnimData>&  animations,
    int pivotX, int pivotY, int fps,
    const SprBuildOptions& opts)
{
    return BuildSprCore(frames, animations, pivotX, pivotY, fps, opts);
}
