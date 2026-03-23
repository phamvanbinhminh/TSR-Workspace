#pragma once
#include <cstdint>

// ================================================================
//  PacketBase — nền tảng chung cho mọi gói tin trong DaiGiangVoTan
// ================================================================
//
//  Cách dùng:
//    1. Mỗi protocol header gộp vào struct này (PacketHeader).
//    2. Dùng InitPacket<T>(pkt, opcode) để khởi tạo bất kỳ packet.
//    3. Dùng PacketOpcode để ép kiểu opcode khi cần.
//
// ================================================================

#pragma pack(push, 1)

// Header chung — PHẢI là field đầu tiên trong mọi packet struct
struct PacketHeader
{
    uint16_t opcode;   // Mã lệnh (enum của từng protocol)
    uint16_t size;     // Tổng bytes của packet (sizeof struct đầy đủ)
};

#pragma pack(pop)

// ── Helper template ──────────────────────────────────────────────
// T là struct packet có field "PacketHeader header" ở đầu
template<typename T>
inline void InitPacket(T& pkt, uint16_t opcode)
{
    pkt.header.opcode = opcode;
    pkt.header.size   = static_cast<uint16_t>(sizeof(T));
}

// Overload cho trường hợp size khác sizeof(T) (dynamic payload)
template<typename T>
inline void InitPacket(T& pkt, uint16_t opcode, uint16_t totalSize)
{
    pkt.header.opcode = opcode;
    pkt.header.size   = totalSize;
}
