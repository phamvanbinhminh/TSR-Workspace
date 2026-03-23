#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include "../../CommonProtocol/GameProtocol.h"
#include "ResManager/Map/Map.h"

// ================================================================
//  GameSession — dữ liệu 1 player đang online
// ================================================================
struct GameSession
{
    uint32_t    playerID   = 0;
    std::string username;
    float       x          = 640.f;
    float       y          = 360.f;
    Appearance  appearance;         // Ngoại trang nhân vật
    uint8_t     animState  = 0;     // AnimState enum (0=idle, ...)
    uintptr_t   sock       = 0;     // SOCKET
    std::string token;
};

// ================================================================
//  GameServer — TCP, port 9001
//  Mỗi client kết nối chạy trên 1 thread riêng
//  Broadcast vị trí player tới tất cả clients còn lại
// ================================================================
class GameServer
{
public:
    GameServer(int port = 9001);
    ~GameServer();

    bool Start();
    void Stop();
    bool IsRunning() const { return _running.load(); }

    // Gọi từ thread chính — chấp nhận kết nối mới (non-blocking)
    void Poll();

    // Kick player đang online theo username (gọi từ LoginServer khi login lại)
    // Gửi S2C_KICKED rồi đóng socket → ClientThread cũ tự cleanup
    void KickByUsername(const std::string& username, const std::string& reason = "Dang nhap o noi khac");

    // Load map server-side (RegionS: collision data)
    // Gọi trước Start() hoặc ngay trong Start()
    void LoadMap(const std::string& folder);

private:
    // Map server-side (RegionS: obstacle/collision)
    std::unique_ptr<Map> _map;

    int               _port;
    std::atomic<bool> _running {false};
    uintptr_t         _listenSock {0};
    uint32_t          _nextPlayerID {1};

    // players online: playerID → GameSession
    std::unordered_map<uint32_t, GameSession> _sessions;

    // tra cứu nhanh: username → playerID (để kick duplicate login)
    std::unordered_map<std::string, uint32_t> _usernameToPlayerID;

    std::mutex _sessionMutex;

    // Xử lý 1 client trên thread riêng
    void ClientThread(uintptr_t clientSock);

    // Broadcast 1 gói đến tất cả trừ excludeID
    void BroadcastExcept(const void* data, int size, uint32_t excludeID);

    // Broadcast đến tất cả kể cả excludeID = 0
    void BroadcastAll(const void* data, int size);

    // Gửi danh sách player hiện tại đến 1 socket
    void SendPlayerList(uintptr_t sock);

    // Gửi join notification cho các player khác
    void BroadcastPlayerJoin(const GameSession& s);

    // Gửi leave notification
    void BroadcastPlayerLeave(const GameSession& s);
};
