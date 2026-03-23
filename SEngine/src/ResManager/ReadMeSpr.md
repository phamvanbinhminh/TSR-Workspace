2. SPR File Format

SPR là sprite animation container dùng cho game 2D.

Một file .spr có thể chứa:

nhiều frame

nhiều animation

metadata (hitbox, pivot)

palette (nếu dùng indexed color)

compression

2.1 SPR Layout
[SPR Header]
[Animation Table]
[Frame Table]
[Frame Metadata]
[Palette] (optional)
[Frame Data]
2.2 SPR Header
struct SprHeader
{
    char magic[4];     // "SPR0"
    uint32 version;

    uint16 frameCount;
    uint16 animationCount;

    uint16 width;
    uint16 height;

    int16 pivotX;   // điểm gốc khi render
    int16 pivotY;

    uint8 pixelFormat;
    uint8 compression;
    uint8 flags;
    uint8 reserved;

    uint32 animationTableOffset;
    uint32 frameTableOffset;
    uint32 metadataOffset;
    uint32 paletteOffset;
    uint32 frameDataOffset;

    uint32 crc32;
};
2.3 Pixel Format
0 = RGBA8
1 = RGB565
2 = INDEX8
3 = GRAYSCALE
2.4 Compression
0 = NONE
1 = RLE
2 = ZLIB
3 = LZ4
2.5 Animation Table
struct SprAnimation
{
    char name[32];

    uint16 startFrame;
    uint16 frameCount;

    uint16 fps;
    uint8 loop;

    uint8 reserved;
};

Ví dụ animation:

idle
walk
attack
die
2.6 Frame Table
struct SprFrameEntry
{
    uint32 offset;
    uint32 size;

    uint16 width;
    uint16 height;

    int16 offsetX;
    int16 offsetY;

    uint16 duration;
};

offsetX / offsetY giúp:

căn sprite

root motion

effect

2.7 Frame Metadata

Dùng cho gameplay (hitbox, collision).

struct SprFrameMeta
{
    int16 hitboxX;
    int16 hitboxY;

    int16 hitboxWidth;
    int16 hitboxHeight;

    int16 attackBoxX;
    int16 attackBoxY;

    int16 attackBoxWidth;
    int16 attackBoxHeight;
};
2.8 Palette (Optional)

Chỉ tồn tại khi pixelFormat = INDEX8.

struct Palette
{
    uint32 colors[256];
};

Format:

RGBA8888
2.9 Frame Data

Pixel data của từng frame.

RGBA8
width * height * 4 bytes
RGB565
width * height * 2 bytes
INDEX8
width * height bytes
2.10 RLE Compression

Run-length encoding:

[count][pixel]
[count][pixel]
[count][pixel]

Ví dụ:

AAAAABBBBCCCC

Encoded:

5A 4B 4C
3. Rendering Rules

Khi render sprite:

gốc sprite nằm tại (pivotX, pivotY)

frame offset (offsetX, offsetY) được cộng thêm vào vị trí render

animation chạy theo fps

Ví dụ:

drawX = positionX - pivotX + offsetX
drawY = positionY - pivotY + offsetY
4. Engine Pipeline
PakArchive
     ↓
Decrypt
     ↓
Decompress
     ↓
SpriteLoader
     ↓
Texture Upload (GPU)
     ↓
SpriteRenderer
5. Asset Pipeline
PNG sequence
     ↓
SPR Builder Tool
     ↓
SPR file
     ↓
PAK packer
     ↓
game.pak