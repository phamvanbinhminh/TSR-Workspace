#pragma once
#include <cstdint>

// ================================================================
//  Appearance — ngoại trang nhân vật
//  Mỗi field là ID của asset (0 = không dùng slot đó)
//  Sprite path convention:
//      res/char/body/{bodyID}.spr
//      res/char/hair/{hairID}.spr
//      res/char/top/{topID}.spr
//      res/char/bottom/{bottomID}.spr
//      res/char/righthand/{rightHandID}.spr
//      res/char/lefthand/{leftHandID}.spr
//      res/char/helmet/{helmetID}.spr
//      res/char/wing/{wingID}.spr
// ================================================================
#pragma pack(push, 1)
struct Appearance
{
    uint16_t bodyID      = 1;   // thân/skin (mặc định = 1)
    uint16_t hairID      = 1;   // tóc
    uint16_t topID       = 0;   // áo (0 = trần)
    uint16_t bottomID    = 0;   // quần
    uint16_t rightHandID = 0;   // tay phải (vũ khí)
    uint16_t leftHandID  = 0;   // tay trái / khiên
    uint16_t helmetID    = 0;   // mũ
    uint16_t wingID      = 0;   // cánh / hiệu ứng lưng
};
#pragma pack(pop)

// Appearance mặc định khi tạo nhân vật mới
inline Appearance DefaultAppearance()
{
    Appearance a{};
    a.bodyID = 1;
    a.hairID = 1;
    return a;
}
