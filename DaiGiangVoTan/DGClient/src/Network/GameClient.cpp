#include "GameClient.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <cstring>
#include <vector>

// ================================================================
//  Constructor / Destructor
// ================================================================

GameClient::GameClient(const std::string& serverIP, int port)
    : _serverIP(serverIP), _port(port)
{
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
}

GameClient::~GameClient()
{
    Disconnect();
    WSACleanup();
}

// ================================================================
//  Connect — kết nối + gửi C2S_Join
// ================================================================

bool GameClient::Connect(const std::string& sessionToken,
                          const std::string& username,
                          float spawnX, float spawnY)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((u_short)_port);
    inet_pton(AF_INET, _serverIP.c_str(), &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        std::cerr << "[GameClient] Không thể kết nối GameServer "
                  << _serverIP << ":" << _port << "\n";
        closesocket(sock);
        return false;
    }

    _sock = (uintptr_t)sock;

    // Gửi C2S_Join
    C2S_Join joinPkt{};
    InitPacket(joinPkt, (uint16_t)C2S_JOIN);
    strncpy_s(joinPkt.sessionToken, sessionToken.c_str(), sizeof(joinPkt.sessionToken) - 1);
    strncpy_s(joinPkt.username,     username.c_str(),     sizeof(joinPkt.username) - 1);
    joinPkt.x = spawnX;
    joinPkt.y = spawnY;
    send(sock, (const char*)&joinPkt, sizeof(joinPkt), 0);

    // Nhận S2C_JoinResult
    S2C_JoinResult res{};
    int r = recv(sock, (char*)&res, sizeof(res), MSG_WAITALL);
    if (r != sizeof(res) || res.result != 1)
    {
        res.message[63] = '\0';
        std::cerr << "[GameClient] Join thất bại: " << res.message << "\n";
        closesocket(sock);
        _sock = 0;
        return false;
    }

    _myPlayerID = res.playerID;
    _connected  = true;
    std::cout << "[GameClient] Join OK. PlayerID=" << _myPlayerID << "\n";

    // Nhận S2C_PlayerList
    {
        PacketHeader hdr{};
        int rh = recv(sock, (char*)&hdr, sizeof(hdr), MSG_WAITALL);
        if (rh == sizeof(hdr) && hdr.opcode == S2C_PLAYER_LIST)
        {
            int bodySize = hdr.size - (int)sizeof(hdr);
            std::vector<char> body(bodySize > 0 ? bodySize : 1);
            if (bodySize > 0)
                recv(sock, body.data(), bodySize, MSG_WAITALL);

            uint16_t count = 0;
            if (bodySize >= (int)sizeof(uint16_t))
            {
                memcpy(&count, body.data(), sizeof(uint16_t));
                int offset = sizeof(uint16_t);
                for (int i = 0; i < count; ++i)
                {
                    if (offset + (int)sizeof(PlayerInfo) > bodySize) break;
                    PlayerInfo pi{};
                    memcpy(&pi, body.data() + offset, sizeof(PlayerInfo));
                    offset += sizeof(PlayerInfo);

                    // Bỏ qua bản thân (tránh render nhân vật mình ở spawn)
                    if (pi.playerID == _myPlayerID) continue;

                    RemotePlayer rp;
                    rp.playerID   = pi.playerID;
                    rp.username   = pi.username;
                    rp.x          = pi.x;
                    rp.y          = pi.y;
                    rp.appearance = pi.appearance;
                    rp.animState  = ANIM_IDLE;

                    std::lock_guard<std::mutex> lk(_mutex);
                    _remotePlayers[pi.playerID] = rp;
                }
            }
            std::cout << "[GameClient] Nhận danh sách " << count << " player.\n";
        }
    }

    // Khởi động receive thread
    _recvThread = std::thread([this]() { ReceiveLoop(); });

    return true;
}

// ================================================================
//  Disconnect
// ================================================================

void GameClient::Disconnect()
{
    if (_connected.exchange(false))
    {
        C2S_Leave leavePkt{};
        InitPacket(leavePkt, (uint16_t)C2S_LEAVE);
        send((SOCKET)_sock, (const char*)&leavePkt, sizeof(leavePkt), 0);

        closesocket((SOCKET)_sock);
        _sock = 0;

        if (_recvThread.joinable())
            _recvThread.join();

        std::cout << "[GameClient] Đã ngắt kết nối.\n";
    }
}

// ================================================================
//  SendMove
// ================================================================

void GameClient::SendMove(float x, float y, uint8_t animState,
                           int16_t hitboxX, int16_t hitboxY,
                           int16_t hitboxW, int16_t hitboxH)
{
    if (!_connected) return;

    C2S_Move pkt{};
    InitPacket(pkt, (uint16_t)C2S_MOVE);
    pkt.playerID  = _myPlayerID;
    pkt.x         = x;
    pkt.y         = y;
    pkt.animState = animState;
    pkt.hitboxX   = hitboxX;
    pkt.hitboxY   = hitboxY;
    pkt.hitboxW   = hitboxW;
    pkt.hitboxH   = hitboxH;
    send((SOCKET)_sock, (const char*)&pkt, sizeof(pkt), 0);
}

// ================================================================
//  GetRemotePlayers (thread-safe snapshot)
// ================================================================

std::unordered_map<uint32_t, RemotePlayer> GameClient::GetRemotePlayers() const
{
    std::lock_guard<std::mutex> lk(_mutex);
    return _remotePlayers;
}

// ================================================================
//  ReceiveLoop — background thread
// ================================================================

void GameClient::ReceiveLoop()
{
    SOCKET sock = (SOCKET)_sock;

    while (_connected)
    {
        PacketHeader hdr{};
        int r = recv(sock, (char*)&hdr, sizeof(hdr), MSG_WAITALL);
        if (r <= 0) break;

        int bodySize = hdr.size - (int)sizeof(hdr);
        if (bodySize < 0 || bodySize > 65535) break;

        std::vector<char> body(bodySize > 0 ? bodySize : 1);
        if (bodySize > 0)
        {
            int rb = recv(sock, body.data(), bodySize, MSG_WAITALL);
            if (rb != bodySize) break;
        }

        HandlePacket(hdr, body.data());
    }

    _connected = false;
    std::cout << "[GameClient] ReceiveLoop kết thúc.\n";
}

// ================================================================
//  HandlePacket
// ================================================================

void GameClient::HandlePacket(const PacketHeader& hdr, const char* body)
{
    switch (hdr.opcode)
    {
    case S2C_PLAYER_JOIN:
    {
        if (hdr.size < (int)(sizeof(PacketHeader) + sizeof(PlayerInfo))) break;
        const PlayerInfo* pi = reinterpret_cast<const PlayerInfo*>(body);
        if (pi->playerID == _myPlayerID) break;

        RemotePlayer rp;
        rp.playerID  = pi->playerID;
        rp.username  = pi->username;
        rp.x         = pi->x;
        rp.y         = pi->y;
        rp.appearance = pi->appearance;  // ← lưu Appearance

        {
            std::lock_guard<std::mutex> lk(_mutex);
            _remotePlayers[pi->playerID] = rp;
        }
        std::cout << "[GameClient] Player join: " << rp.username
                  << " ID=" << rp.playerID
                  << " body=" << rp.appearance.bodyID << "\n";
        break;
    }

    case S2C_PLAYER_MOVE:
    {
        // body layout: S2C_PlayerMove minus header
        if (hdr.size - (int)sizeof(hdr) < (int)(sizeof(uint32_t) + 2 * sizeof(float))) break;

        uint32_t pid;
        float nx, ny;
        uint8_t animSt = ANIM_IDLE;

        int off = 0;
        memcpy(&pid,    body + off, sizeof(uint32_t)); off += sizeof(uint32_t);
        memcpy(&nx,     body + off, sizeof(float));    off += sizeof(float);
        memcpy(&ny,     body + off, sizeof(float));    off += sizeof(float);

        // Đọc animState nếu gói đủ lớn
        if (hdr.size - (int)sizeof(hdr) >= off + (int)sizeof(uint8_t))
            memcpy(&animSt, body + off, sizeof(uint8_t));

        std::lock_guard<std::mutex> lk(_mutex);
        auto it = _remotePlayers.find(pid);
        if (it != _remotePlayers.end())
        {
            it->second.x         = nx;
            it->second.y         = ny;
            it->second.animState = animSt;
        }
        break;
    }

    case S2C_PLAYER_LEAVE:
    {
        if (hdr.size - (int)sizeof(hdr) < (int)sizeof(uint32_t)) break;
        uint32_t pid;
        memcpy(&pid, body, sizeof(uint32_t));
        {
            std::lock_guard<std::mutex> lk(_mutex);
            _remotePlayers.erase(pid);
        }
        std::cout << "[GameClient] Player leave ID=" << pid << "\n";
        break;
    }

    case S2C_KICKED:
    {
        // Server kick mình (login ở nơi khác)
        char reason[65] = {};
        if (hdr.size - (int)sizeof(hdr) >= 64)
            memcpy(reason, body, 64);
        reason[64] = '\0';
        std::cout << "[GameClient] Bị kick: " << reason << "\n";
        {
            std::lock_guard<std::mutex> lk(_mutex);
            _kickReason = reason;
            _wasKicked  = true;
        }
        // Kết thúc recv loop để cleanup
        _connected = false;
        break;
    }

    default:
        break;
    }
}
