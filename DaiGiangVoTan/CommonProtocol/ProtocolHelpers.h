#pragma once
#include "LoginProtocol.h"

inline void InitHeader(PacketHeader& hdr, Opcode op, uint16_t totalSize)
{
    hdr.opcode = static_cast<uint16_t>(op);
    hdr.size   = totalSize;
}
