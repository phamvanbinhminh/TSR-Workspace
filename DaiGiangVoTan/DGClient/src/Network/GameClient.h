#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include "../../CommonProtocol/GameProtocol.h"

// ================================================================
//  RemotePlayer — dữ liệu 1 player khác đang online
// ================================================================
struct RemotePlayer
{
    uint32_t    playerID   = 0;
    std::string username;
    float       x          = 0.f;
    float       y          = 0.f;
    Appearance  appearance;          // Ngoại trang (để client render đúng skin)
    uint8_t     animState  = 0;      // AnimState enum (0=idle, 1=walk_left, ...)
};

// ================================================================
//  GameClient — kết nối TCP đến GameServer (port 9001)
//  Gửi Join/Move, nhận broadcast từ server
//  Thread-safe (receive chạy trên background thread)
// ================================================================
class GameClient
{
public:
    GameClient(const std::string& serverIP = "127.0.0.1", int port = 9001);
    ~GameClient();

    // Kết nối + gửi C2S_Join, trả về false nếu token sai
    bool Connect(const std::string& sessionToken,
                 const std::string& username,
                 float spawnX = 640.f, float spawnY = 360.f);

    // Disconnect
    void Disconnect();

    bool IsConnected() const { return _connected.load(); }

    // Gửi vị trí + animation state đến server (gọi từ game thread)
    void SendMove(float x, float y, uint8_t animState = ANIM_IDLE);

    // Lấy playerID của mình (do server cấp)
    uint32_t GetMyPlayerID() const { return _myPlayerID; }

    // Lấy snapshot danh sách remote players (thread-safe copy)
    std::unordered_map<uint32_t, RemotePlayer> GetRemotePlayers() const;

    // Kiểm tra có bị kick không (do login ở nơi khác)
    bool WasKicked() const { return _wasKicked.load(); }
    std::string GetKickReason() const
    {
        std::lock_guard<std::mutex> lk(_mutex);
        return _kickReason;
    }

private:
    std::string _serverIP;
    int         _port;

    uintptr_t   _sock       {0};
    uint32_t    _myPlayerID {0};
    std::atomic<bool> _connected {false};

    mutable std::mutex _mutex;
    std::unordered_map<uint32_t, RemotePlayer> _remotePlayers;

    // Trạng thái bị kick
    std::atomic<bool> _wasKicked  {false};
    std::string       _kickReason;        // protected by _mutex

    std::thread _recvThread;

    void ReceiveLoop();
    void HandlePacket(const PacketHeader& hdr, const char* body);
};
