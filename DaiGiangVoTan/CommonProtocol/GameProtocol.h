#pragma once
#include "PacketBase.h"
#include "Appearance.h"

// ================================================================
//  Game Opcodes (port 9001)
// ================================================================
enum GameOpcode : uint16_t
{
    // Client → Server
    C2S_JOIN         = 0x0101,  // Vào game (gửi sessionToken + username)
    C2S_MOVE         = 0x0102,  // Gửi vị trí mới của mình
    C2S_LEAVE        = 0x0103,  // Rời game (tùy chọn)

    // Server → Client
    S2C_JOIN_RESULT  = 0x0201,  // Kết quả khi vào game (playerID được gán)
    S2C_PLAYER_LIST  = 0x0202,  // Danh sách player hiện tại trong map
    S2C_PLAYER_JOIN  = 0x0203,  // Có player mới vào
    S2C_PLAYER_MOVE  = 0x0204,  // Vị trí của 1 player thay đổi
    S2C_PLAYER_LEAVE = 0x0205,  // 1 player rời game
    S2C_KICKED       = 0x0206,  // Server kick client (login ở nơi khác)
};

// ================================================================
//  Packet structs — đều dùng PacketHeader chung từ PacketBase.h
// ================================================================
#pragma pack(push, 1)

// ── C2S_Join ────────────────────────────────────────────────────
struct C2S_Join
{
    PacketHeader header;        // opcode = C2S_JOIN
    char sessionToken[64];
    char username[32];
    float x;
    float y;
};

// ── S2C_JoinResult ──────────────────────────────────────────────
struct S2C_JoinResult
{
    PacketHeader header;        // opcode = S2C_JOIN_RESULT
    int32_t      result;        // 1 = OK, -1 = token không hợp lệ
    uint32_t     playerID;
    char         message[64];
};

// ── PlayerInfo — dùng trong PlayerList + PlayerJoin ─────────────
struct PlayerInfo
{
    uint32_t   playerID;
    char       username[32];
    float      x;
    float      y;
    Appearance appearance;  // Ngoại trang nhân vật
};

// ── S2C_PlayerList ──────────────────────────────────────────────
// Payload: S2C_PlayerList header (count) + count * PlayerInfo
struct S2C_PlayerList
{
    PacketHeader header;        // opcode = S2C_PLAYER_LIST
    uint16_t     count;
    // PlayerInfo players[count] nằm sau trong buffer
};

// ── S2C_PlayerJoin ──────────────────────────────────────────────
struct S2C_PlayerJoin
{
    PacketHeader header;        // opcode = S2C_PLAYER_JOIN
    PlayerInfo   player;
};

// ── AnimState — trạng thái animation để sync ────────────────────
// Phải khớp với tên animation trong file .spr (player.lua)
enum AnimState : uint8_t
{
    ANIM_IDLE_DOWN  = 0,   // "idle_down"  (mặc định)
    ANIM_IDLE_LEFT  = 1,   // "idle_left"
    ANIM_IDLE_RIGHT = 2,   // "idle_right"
    ANIM_IDLE_UP    = 3,   // "idle_up"
    ANIM_WALK_LEFT  = 4,   // "walk_left"
    ANIM_WALK_RIGHT = 5,   // "walk_right"
    ANIM_WALK_UP    = 6,   // "walk_up"
    ANIM_WALK_DOWN  = 7,   // "walk_down"

    ANIM_IDLE = ANIM_IDLE_DOWN,  // alias cho default
};

// ── C2S_Move ────────────────────────────────────────────────────
struct C2S_Move
{
    PacketHeader header;        // opcode = C2S_MOVE
    uint32_t     playerID;
    float        x;
    float        y;
    uint8_t      animState;     // AnimState enum
    uint8_t      _pad[3];       // padding để align

    // Hitbox của entity (tương đối so với vị trí x,y)
    // Client lấy từ SpriteComponent::GetHitbox()
    // Server dùng để check collision với map obstacles
    int16_t      hitboxX;       // offset X từ x
    int16_t      hitboxY;       // offset Y từ y
    int16_t      hitboxW;       // width
    int16_t      hitboxH;       // height
};

// ── S2C_PlayerMove ──────────────────────────────────────────────
struct S2C_PlayerMove
{
    PacketHeader header;        // opcode = S2C_PLAYER_MOVE
    uint32_t     playerID;
    float        x;
    float        y;
    uint8_t      animState;     // AnimState enum
    uint8_t      _pad[3];       // padding để align
};

// ── C2S_Leave ───────────────────────────────────────────────────
struct C2S_Leave
{
    PacketHeader header;        // opcode = C2S_LEAVE
};

// ── S2C_PlayerLeave ─────────────────────────────────────────────
struct S2C_PlayerLeave
{
    PacketHeader header;        // opcode = S2C_PLAYER_LEAVE
    uint32_t     playerID;
    char         username[32];
};

// ── S2C_Kicked ──────────────────────────────────────────────────
// Server gửi khi kick client (vd: login lại ở nơi khác)
struct S2C_Kicked
{
    PacketHeader header;        // opcode = S2C_KICKED
    char         reason[64];    // Lý do bị kick
};

#pragma pack(pop)
