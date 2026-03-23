#pragma once
#include "Scene/IScene.h"
#include "ECS/Scene/Scene.h"
#include "ECS/Object/Object.h"
#include "ECS/Components/SpriteComponent.h"
#include "Renderer/Renderer.h"
#include "../Network/GameClient.h"
#include "../../../CommonProtocol/Appearance.h"
#include "ResManager/Map/Map.h"

#include <string>
#include <memory>
#include <unordered_map>
#include <functional>

struct GLFWwindow;

// ============================================================
// RemotePlayerSprite — sprite standalone cho remote player
// Không gắn vào ECS, chỉ dùng để render
// ============================================================
struct RemotePlayerSprite
{
    Object          dummyObject;   // owner placeholder cho SpriteComponent
    SpriteComponent sprite;        // sprite component

    // Interpolated render position (lerp về target mỗi frame)
    float renderX = 0.f;
    float renderY = 0.f;

    RemotePlayerSprite(Renderer* renderer)
        : sprite(&dummyObject, renderer)
    {}

    // Không cho copy/move vì sprite._owner trỏ vào dummyObject (địa chỉ cố định)
    RemotePlayerSprite(const RemotePlayerSprite&)            = delete;
    RemotePlayerSprite& operator=(const RemotePlayerSprite&) = delete;
    RemotePlayerSprite(RemotePlayerSprite&&)                 = delete;
    RemotePlayerSprite& operator=(RemotePlayerSprite&&)      = delete;
};

// ============================================================
// GameScene — scene chính sau khi login thành công
// ============================================================
// - Mount VFS (nếu chưa mount)
// - Tạo player entity với Transform + Sprite + Script (ECS local)
//   spawn tại đúng vị trí lần cuối từ server
// - Kết nối GameServer (port 9001) với sessionToken
// - Render remote players dùng sprite main.spr (skeleton)
// - Gửi vị trí mình đến server mỗi frame
// ============================================================
class GameScene : public IScene
{
public:
    GameScene(GLFWwindow* window, Renderer* renderer,
              const std::string& username,
              const std::string& sessionToken = "",
              const std::string& serverIP     = "127.0.0.1",
              int screenW = 1280, int screenH = 720,
              float spawnX     = 640.f,
              float spawnY     = 360.f,
              Appearance appearance = {});

    // IScene interface
    void Init()           override;
    void Update(float dt) override;
    void Render()         override;
    void Destroy()        override;

    // Đặt callback được gọi khi bị server kick (login ở nơi khác)
    // reason: lý do kick
    void SetOnKickedCallback(std::function<void(const std::string& reason)> cb)
    {
        _onKicked = std::move(cb);
    }

private:
    GLFWwindow*  _window       = nullptr;
    Renderer*    _renderer     = nullptr;
    std::string  _username;
    std::string  _sessionToken;
    std::string  _serverIP;
    int          _screenW      = 1280;
    int          _screenH      = 720;

    // Spawn position (từ server: vị trí lần cuối)
    float        _spawnX       = 640.f;
    float        _spawnY       = 360.f;

    // Appearance nhân vật chính (từ server)
    Appearance   _appearance;

    // ECS scene (local, nhân vật chính)
    Scene        _ecsScene;

    // Multiplayer
    GameClient   _gameClient;

    // Thời gian tích lũy để gửi vị trí theo interval
    float        _sendTimer    = 0.f;
    float        _sendInterval = 0.05f;  // 20 Hz

    // Callback khi bị kick
    std::function<void(const std::string&)> _onKicked;

    // Cache sprite cho remote players (playerID → sprite)
    std::unordered_map<uint32_t, std::unique_ptr<RemotePlayerSprite>> _remoteSprites;

    // Map (client-side: region_C lazy-load)
    std::unique_ptr<Map> _map;
    std::string          _mapFolder;   // e.g. "maps/Home"

    // Load sprite skeleton (main.spr) cho 1 remote player
    void LoadRemoteSprite(uint32_t playerID);

    // Tạo nhân vật chính tại vị trí spawn từ server
    void SpawnPlayer();

    // Lấy vị trí nhân vật chính hiện tại
    bool GetPlayerPos(float& outX, float& outY) const;

    // Load map từ folder
    void LoadMap(const std::string& folder);
};
