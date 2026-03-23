#include "GameServer.h"
#include "LoginServer.h"              // SessionManager::Get()
#include "../Auth/CharacterManager.h" // CharacterManager::Get()

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <cstring>
#include <vector>
#include <filesystem>

// ================================================================
//  Constructor / Destructor
// ================================================================

GameServer::GameServer(int port)
    : _port(port)
    , _listenSock((uintptr_t)INVALID_SOCKET)
{
}

GameServer::~GameServer()
{
    Stop();
}

// ================================================================
//  LoadMap
// ================================================================

void GameServer::LoadMap(const std::string& folder)
{
    if (folder.empty()) return;

    _map = std::make_unique<Map>();
    if (_map->Load(folder))
        std::cout << "[GameServer] Map loaded: " << folder
                  << "  " << _map->GetWidth() << "x" << _map->GetHeight()
                  << " unit=" << _map->GetUnitSize() << "\n";
    else
    {
        std::cerr << "[GameServer] Map load failed: " << folder << "\n";
        _map.reset();
    }
}

// ================================================================
//  Start / Stop
// ================================================================

bool GameServer::Start()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        std::cerr << "[GameServer] Không thể tạo socket.\n";
        return false;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((u_short)_port);

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        std::cerr << "[GameServer] Bind thất bại trên port " << _port << ".\n";
        closesocket(sock);
        return false;
    }

    if (listen(sock, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "[GameServer] Listen thất bại.\n";
        closesocket(sock);
        return false;
    }

    // Non-blocking để Poll() không block
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    _listenSock = (uintptr_t)sock;
    _running    = true;
    std::cout << "[GameServer] Lắng nghe trên port " << _port << "...\n";
    return true;
}

void GameServer::Stop()
{
    if (_running.exchange(false))
    {
        closesocket((SOCKET)_listenSock);
        std::cout << "[GameServer] Đã dừng.\n";
    }
}

// ================================================================
//  Poll — gọi từ thread chính, chấp nhận kết nối mới
// ================================================================

void GameServer::Poll()
{
    if (!_running) return;

    SOCKET clientSock = accept((SOCKET)_listenSock, nullptr, nullptr);
    if (clientSock == INVALID_SOCKET)
        return; // WSAEWOULDBLOCK = không có kết nối mới

    // Trả về blocking mode cho client socket
    u_long mode = 0;
    ioctlsocket(clientSock, FIONBIO, &mode);

    char clientIP[64] = "?";
    sockaddr_in ca{};
    int calen = sizeof(ca);
    getpeername(clientSock, (sockaddr*)&ca, &calen);
    inet_ntop(AF_INET, &ca.sin_addr, clientIP, sizeof(clientIP));
    std::cout << "[GameServer] Kết nối mới từ " << clientIP << "\n";

    uintptr_t csock = (uintptr_t)clientSock;
    std::thread([this, csock]() { ClientThread(csock); }).detach();
}

// ================================================================
//  ClientThread — xử lý 1 client
// ================================================================

void GameServer::ClientThread(uintptr_t clientSock)
{
    uint32_t myID = 0;

    // ── Bước 1: Nhận C2S_Join ─────────────────────────────────
    C2S_Join joinPkt{};
    int r = recv((SOCKET)clientSock, (char*)&joinPkt, sizeof(joinPkt), MSG_WAITALL);
    if (r != sizeof(joinPkt) || joinPkt.header.opcode != C2S_JOIN)
    {
        std::cerr << "[GameServer] Gói join không hợp lệ.\n";
        closesocket((SOCKET)clientSock);
        return;
    }

    joinPkt.sessionToken[63] = '\0';
    joinPkt.username[31]     = '\0';

    // Validate session token
    std::string tokenStr(joinPkt.sessionToken);
    std::string ownerUser = SessionManager::Get().ValidateToken(tokenStr);

    S2C_JoinResult joinResult{};
    InitPacket(joinResult, (uint16_t)S2C_JOIN_RESULT);

    if (ownerUser.empty())
    {
        joinResult.result = -1;
        strncpy_s(joinResult.message, "Token khong hop le!", sizeof(joinResult.message) - 1);
        send((SOCKET)clientSock, (const char*)&joinResult, sizeof(joinResult), 0);
        closesocket((SOCKET)clientSock);
        std::cerr << "[GameServer] Token rejected.\n";
        return;
    }

    // ── Bước 2: Load CharacterData từ file, cấp playerID ─────
    std::string username = joinPkt.username[0] ? joinPkt.username : ownerUser;
    CharacterData charData = CharacterManager::Get().LoadOrCreate(username);

    // ── Kick player cũ nếu username đã đang online ────────────
    {
        std::lock_guard<std::mutex> lk(_sessionMutex);
        auto dupIt = _usernameToPlayerID.find(username);
        if (dupIt != _usernameToPlayerID.end())
        {
            uint32_t oldID = dupIt->second;
            auto sessIt = _sessions.find(oldID);
            if (sessIt != _sessions.end())
            {
                std::cout << "[GameServer] Kick duplicate login: "
                          << username << " (playerID=" << oldID << ")\n";
                // Gửi S2C_KICKED để client cũ biết lý do ngắt kết nối
                S2C_Kicked kickPkt{};
                InitPacket(kickPkt, (uint16_t)S2C_KICKED);
                strncpy_s(kickPkt.reason, "Dang nhap o noi khac!", sizeof(kickPkt.reason) - 1);
                send((SOCKET)sessIt->second.sock, (const char*)&kickPkt, sizeof(kickPkt), 0);
                // Đóng socket cũ → ClientThread cũ sẽ tự break ra và cleanup
                closesocket((SOCKET)sessIt->second.sock);
            }
            // Xóa entry ngay để tránh race khi ClientThread cũ chưa cleanup xong
            _sessions.erase(oldID);
            _usernameToPlayerID.erase(dupIt);
        }

        myID = _nextPlayerID++;
        GameSession& s = _sessions[myID];
        s.playerID  = myID;
        s.username  = username;
        s.x         = charData.posX;
        s.y         = charData.posY;
        s.appearance = charData.appearance;
        s.sock      = clientSock;
        s.token     = tokenStr;

        _usernameToPlayerID[username] = myID;
    }

    std::cout << "[GameServer] Player join: " << username
              << " ID=" << myID
              << " body=" << charData.appearance.bodyID
              << "\n";

    // Gửi kết quả join
    joinResult.result   = 1;
    joinResult.playerID = myID;
    strncpy_s(joinResult.message, "Chao mung vao game!", sizeof(joinResult.message) - 1);
    send((SOCKET)clientSock, (const char*)&joinResult, sizeof(joinResult), 0);

    // Gửi danh sách player hiện tại
    SendPlayerList(clientSock);

    // Broadcast mình đã vào
    {
        std::lock_guard<std::mutex> lk(_sessionMutex);
        BroadcastPlayerJoin(_sessions[myID]);
    }

    // ── Bước 3: Vòng lặp nhận packet ──────────────────────────
    while (_running)
    {
        PacketHeader hdr{};
        int rh = recv((SOCKET)clientSock, (char*)&hdr, sizeof(hdr), MSG_WAITALL);
        if (rh <= 0) break;

        if (hdr.opcode == C2S_MOVE)
        {
            C2S_Move movePkt{};
            movePkt.header = hdr;
            int bodySize = sizeof(movePkt) - sizeof(hdr);
            int rm = recv((SOCKET)clientSock,
                          (char*)&movePkt + sizeof(hdr),
                          bodySize, MSG_WAITALL);
            if (rm != bodySize) break;

            // Kiểm tra collision với map (AABB 16x24, gốc ở giữa đáy nhân vật)
            bool blocked = false;
            if (_map)
            {
                constexpr float HW = 8.f;   // half-width
                constexpr float HH = 12.f;  // half-height
                blocked = _map->CheckCollision(
                    movePkt.x - HW, movePkt.y - HH, HW * 2.f, HH * 2.f);
            }

            if (!blocked)
            {
                {
                    std::lock_guard<std::mutex> lk(_sessionMutex);
                    auto it = _sessions.find(myID);
                    if (it != _sessions.end())
                    {
                        it->second.x         = movePkt.x;
                        it->second.y         = movePkt.y;
                        it->second.animState = movePkt.animState;
                    }
                }

                S2C_PlayerMove moveResp{};
                InitPacket(moveResp, (uint16_t)S2C_PLAYER_MOVE);
                moveResp.playerID  = myID;
                moveResp.x         = movePkt.x;
                moveResp.y         = movePkt.y;
                moveResp.animState = movePkt.animState;
                BroadcastExcept(&moveResp, sizeof(moveResp), myID);
            }
            // Nếu blocked: không cập nhật vị trí, không broadcast
            // Client sẽ tự rollback về vị trí server khi không nhận được confirm
        }
        else if (hdr.opcode == C2S_LEAVE)
        {
            break;
        }
        else
        {
            // Opcode không biết — skip body
            int bodyLeft = hdr.size - (int)sizeof(hdr);
            if (bodyLeft > 0 && bodyLeft < 8192)
            {
                std::vector<char> tmp(bodyLeft);
                recv((SOCKET)clientSock, tmp.data(), bodyLeft, MSG_WAITALL);
            }
        }
    }

    // ── Bước 4: Cleanup khi disconnect — lưu vị trí + session ─
    std::cout << "[GameServer] Player disconnect ID=" << myID << "\n";

    GameSession leaveSession;
    {
        std::lock_guard<std::mutex> lk(_sessionMutex);
        auto it = _sessions.find(myID);
        if (it != _sessions.end())
        {
            leaveSession = it->second;
            _sessions.erase(it);
        }

        // Xóa username map chỉ khi vẫn trỏ về myID
        // (nếu đã bị kick trước đó thì entry đã bị xóa/thay thế rồi)
        auto uIt = _usernameToPlayerID.find(leaveSession.username);
        if (uIt != _usernameToPlayerID.end() && uIt->second == myID)
            _usernameToPlayerID.erase(uIt);
    }

    if (leaveSession.playerID)
    {
        // Lưu vị trí cuối cùng vào character file
        CharacterData saveData = CharacterManager::Get().LoadOrCreate(leaveSession.username);
        saveData.posX       = leaveSession.x;
        saveData.posY       = leaveSession.y;
        saveData.appearance = leaveSession.appearance;
        CharacterManager::Get().Save(leaveSession.username, saveData);

        // Xóa session token
        SessionManager::Get().RemoveSession(leaveSession.token);

        BroadcastPlayerLeave(leaveSession);
    }

    closesocket((SOCKET)clientSock);
}

// ================================================================
//  Broadcast helpers
// ================================================================

void GameServer::BroadcastExcept(const void* data, int size, uint32_t excludeID)
{
    std::lock_guard<std::mutex> lk(_sessionMutex);
    for (auto& [id, s] : _sessions)
    {
        if (id == excludeID) continue;
        send((SOCKET)s.sock, (const char*)data, size, 0);
    }
}

void GameServer::BroadcastAll(const void* data, int size)
{
    BroadcastExcept(data, size, 0);
}

void GameServer::SendPlayerList(uintptr_t sock)
{
    std::vector<PlayerInfo> players;
    {
        std::lock_guard<std::mutex> lk(_sessionMutex);
        for (auto& [id, s] : _sessions)
        {
            PlayerInfo pi{};
            pi.playerID = s.playerID;
            strncpy_s(pi.username, s.username.c_str(), sizeof(pi.username) - 1);
            pi.x          = s.x;
            pi.y          = s.y;
            pi.appearance = s.appearance;
            players.push_back(pi);
        }
    }

    uint16_t count     = (uint16_t)players.size();
    uint16_t totalSize = (uint16_t)(sizeof(S2C_PlayerList) + count * sizeof(PlayerInfo));

    std::vector<char> buf(totalSize);
    S2C_PlayerList* listHdr = reinterpret_cast<S2C_PlayerList*>(buf.data());
    InitPacket(*listHdr, (uint16_t)S2C_PLAYER_LIST, totalSize);
    listHdr->count = count;

    if (count > 0)
        memcpy(buf.data() + sizeof(S2C_PlayerList),
               players.data(),
               count * sizeof(PlayerInfo));

    send((SOCKET)sock, buf.data(), totalSize, 0);
}

void GameServer::BroadcastPlayerJoin(const GameSession& s)
{
    S2C_PlayerJoin pkt{};
    InitPacket(pkt, (uint16_t)S2C_PLAYER_JOIN);
    pkt.player.playerID  = s.playerID;
    strncpy_s(pkt.player.username, s.username.c_str(), sizeof(pkt.player.username) - 1);
    pkt.player.x          = s.x;
    pkt.player.y          = s.y;
    pkt.player.appearance = s.appearance;

    for (auto& [id, sess] : _sessions)
    {
        if (id == s.playerID) continue;
        send((SOCKET)sess.sock, (const char*)&pkt, sizeof(pkt), 0);
    }
}

void GameServer::BroadcastPlayerLeave(const GameSession& s)
{
    S2C_PlayerLeave pkt{};
    InitPacket(pkt, (uint16_t)S2C_PLAYER_LEAVE);
    pkt.playerID = s.playerID;
    strncpy_s(pkt.username, s.username.c_str(), sizeof(pkt.username) - 1);
    BroadcastAll(&pkt, sizeof(pkt));
}

// ================================================================
//  KickByUsername — public API, gọi từ LoginServer khi login lại
// ================================================================

void GameServer::KickByUsername(const std::string& username, const std::string& reason)
{
    std::lock_guard<std::mutex> lk(_sessionMutex);
    auto uIt = _usernameToPlayerID.find(username);
    if (uIt == _usernameToPlayerID.end()) return;

    uint32_t oldID = uIt->second;
    auto sessIt = _sessions.find(oldID);
    if (sessIt != _sessions.end())
    {
        std::cout << "[GameServer] KickByUsername: " << username
                  << " (playerID=" << oldID << ") reason=" << reason << "\n";
        // Gửi S2C_KICKED để client hiển thị thông báo
        S2C_Kicked kickPkt{};
        InitPacket(kickPkt, (uint16_t)S2C_KICKED);
        strncpy_s(kickPkt.reason, reason.c_str(), sizeof(kickPkt.reason) - 1);
        send((SOCKET)sessIt->second.sock, (const char*)&kickPkt, sizeof(kickPkt), 0);
        // Đóng socket → ClientThread tự cleanup
        closesocket((SOCKET)sessIt->second.sock);
    }
    // Xóa khỏi maps ngay để slot trống cho login mới
    _sessions.erase(oldID);
    _usernameToPlayerID.erase(uIt);
}
