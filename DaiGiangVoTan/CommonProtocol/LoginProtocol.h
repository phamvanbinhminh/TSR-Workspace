#pragma once
#include "PacketBase.h"
#include "Appearance.h"

// ================================================================
//  Login Opcodes (port 9000)
// ================================================================
enum LoginOpcode : uint16_t
{
    C2S_LOGIN        = 0x0001,  // Client gửi thông tin đăng nhập
    S2C_LOGIN_RESULT = 0x0002,  // Server phản hồi kết quả
};

enum LoginResult : int32_t
{
    LOGIN_SUCCESS         =  1,
    LOGIN_FAIL_WRONG_PASS = -1,   // Sai mật khẩu
    LOGIN_FAIL_NOT_FOUND  = -2,   // Tài khoản không tồn tại
};

// ================================================================
//  Packet structs — đều dùng PacketHeader chung từ PacketBase.h
// ================================================================
#pragma pack(push, 1)

// Client → Server: gửi username + password
struct C2S_Login
{
    PacketHeader header;        // opcode = C2S_LOGIN
    char username[32];
    char password[64];
};

// Server → Client: kết quả đăng nhập
struct S2C_LoginResult
{
    PacketHeader header;        // opcode = S2C_LOGIN_RESULT
    int32_t      result;        // LoginResult enum
    char         message[64];   // Thông báo (nếu thất bại)
    char         sessionToken[64]; // Token dùng để vào GameServer
    // ── Character data trả về khi login thành công ──
    Appearance   appearance;    // Ngoại trang nhân vật
    float        posX;          // Vị trí lần cuối
    float        posY;
    int32_t      level;         // Level nhân vật
    int32_t      mapID;         // ID bản đồ nhân vật đang ở
};

#pragma pack(pop)
