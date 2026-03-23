#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <gl/GL.h>

#include <string>
#include <sstream>
#include <iostream>

#include "Renderer.h"
#include "stb_image.h"

#include <ft2build.h>
#include FT_FREETYPE_H

// ============================================================
//  UTF-8 decoder: đọc 1 codepoint từ chuỗi UTF-8
//  Trả về codepoint, cập nhật con trỏ i đến byte tiếp theo
// ============================================================
static unsigned long NextCodepoint(const std::string& text, size_t& i)
{
    unsigned char c0 = (unsigned char)text[i];

    if (c0 < 0x80)
    {
        // 1 byte: 0xxxxxxx
        i += 1;
        return c0;
    }
    else if ((c0 & 0xE0) == 0xC0)
    {
        // 2 byte: 110xxxxx 10xxxxxx
        if (i + 1 >= text.size()) { i++; return 0xFFFD; }
        unsigned long cp = ((c0 & 0x1F) << 6)
                         | ((unsigned char)text[i + 1] & 0x3F);
        i += 2;
        return cp;
    }
    else if ((c0 & 0xF0) == 0xE0)
    {
        // 3 byte: 1110xxxx 10xxxxxx 10xxxxxx
        if (i + 2 >= text.size()) { i++; return 0xFFFD; }
        unsigned long cp = ((c0 & 0x0F) << 12)
                         | (((unsigned char)text[i + 1] & 0x3F) << 6)
                         | ((unsigned char)text[i + 2] & 0x3F);
        i += 3;
        return cp;
    }
    else if ((c0 & 0xF8) == 0xF0)
    {
        // 4 byte: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        if (i + 3 >= text.size()) { i++; return 0xFFFD; }
        unsigned long cp = ((c0 & 0x07) << 18)
                         | (((unsigned char)text[i + 1] & 0x3F) << 12)
                         | (((unsigned char)text[i + 2] & 0x3F) << 6)
                         | ((unsigned char)text[i + 3] & 0x3F);
        i += 4;
        return cp;
    }

    // byte không hợp lệ
    i++;
    return 0xFFFD;
}

// ============================================================
//  Load 1 glyph theo codepoint, lưu vào map chars
// ============================================================
bool TextRenderer::LoadGlyph(unsigned long codepoint)
{
    if (chars.count(codepoint)) return true;   // đã có
    if (!face) return false;

    if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER))
    {
        std::cerr << "[TextRenderer] Không load được glyph U+" << std::hex << codepoint << "\n";
        return false;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);

    // Dùng GL_ALPHA để tương thích với OpenGL immediate mode (1.x/2.x)
    glTexImage2D(
        GL_TEXTURE_2D, 0, GL_ALPHA,
        face->glyph->bitmap.width,
        face->glyph->bitmap.rows,
        0, GL_ALPHA, GL_UNSIGNED_BYTE,
        face->glyph->bitmap.buffer
    );

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    Character ch;
    ch.texture  = tex;
    ch.width    = face->glyph->bitmap.width;
    ch.height   = face->glyph->bitmap.rows;
    ch.bearingX = face->glyph->bitmap_left;
    ch.bearingY = face->glyph->bitmap_top;
    ch.advance  = face->glyph->advance.x;

    chars[codepoint] = ch;
    return true;
}

// ============================================================
//  DrawText2D — hỗ trợ UTF-8 đầy đủ
// ============================================================
void TextRenderer::DrawText2D(
    const std::string& text,
    float x,
    float y,
    float scale
)
{
    glEnable(GL_TEXTURE_2D);

    // Bật blending để render glyph alpha đúng
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Dùng glColor4f để alpha hoạt động đúng với GL_ALPHA texture
    glColor4f(1.0f, 0.2f, 0.2f, 1.0f);

    size_t i = 0;
    while (i < text.size())
    {
        unsigned long cp = NextCodepoint(text, i);

        // Load glyph on-demand nếu chưa có
        if (!LoadGlyph(cp)) continue;

        const Character& ch = chars[cp];

        float xpos = x + ch.bearingX * scale;
        // Baseline cố định: y là baseline, glyph vẽ lên trên bearingY
        float ypos = y - ch.bearingY * scale;

        float w = ch.width  * scale;
        float h = ch.height * scale;

        glBindTexture(GL_TEXTURE_2D, ch.texture);

        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(xpos,     ypos);
        glTexCoord2f(1, 0); glVertex2f(xpos + w, ypos);
        glTexCoord2f(1, 1); glVertex2f(xpos + w, ypos + h);
        glTexCoord2f(0, 1); glVertex2f(xpos,     ypos + h);
        glEnd();

        x += (ch.advance >> 6) * scale;
    }

    glDisable(GL_BLEND);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f); // reset màu
}

// ============================================================
//  Init — load font, giữ FT_Face alive để load glyph on-demand
// ============================================================
bool TextRenderer::Init(const std::string& fontFile, int size)
{
    if (FT_Init_FreeType(&ft))
    {
        std::cerr << "[TextRenderer] FreeType init failed\n";
        return false;
    }

    if (FT_New_Face(ft, fontFile.c_str(), 0, &face))
    {
        std::cerr << "[TextRenderer] Font load failed: " << fontFile << "\n";
        FT_Done_FreeType(ft);
        return false;
    }

    FT_Set_Pixel_Sizes(face, 0, size);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Pre-load ASCII cơ bản (32-127) để render nhanh
    for (unsigned long c = 32; c < 128; c++)
        LoadGlyph(c);

    // KHÔNG gọi FT_Done_Face / FT_Done_FreeType ở đây
    // vì cần giữ face alive để load glyph Unicode on-demand
    std::cout << "[TextRenderer] Font loaded: " << fontFile << " (UTF-8 ready)\n";
    return true;
}

// ============================================================
//  Destructor — giải phóng FreeType khi TextRenderer bị huỷ
// ============================================================
TextRenderer::~TextRenderer()
{
    if (face)
    {
        FT_Done_Face(face);
        face = nullptr;
    }
    if (ft)
    {
        FT_Done_FreeType(ft);
        ft = nullptr;
    }
}
