#include "GameScene.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/SpriteComponent.h"
#include "ECS/Components/ScriptComponent.h"
#include "ResManager/VFS/VFS.h"
#include "../../../CommonProtocol/GameProtocol.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>
#include <filesystem>

// ── Constructor ───────────────────────────────────────────────────────────────

GameScene::GameScene(GLFWwindow* window, Renderer* renderer,
                     const std::string& username,
                     const std::string& sessionToken,
                     const std::string& serverIP,
                     int screenW, int screenH,
                     float spawnX,
                     float spawnY,
                     Appearance appearance)
    : _window(window)
    , _renderer(renderer)
    , _username(username)
    , _sessionToken(sessionToken)
    , _serverIP(serverIP)
    , _screenW(screenW)
    , _screenH(screenH)
    , _spawnX(spawnX)
    , _spawnY(spawnY)
    , _appearance(appearance)
    , _gameClient(serverIP, 9001)
{
}

// ── IScene interface ─────────────────────────────────────────────────────────

void GameScene::LoadMap(const std::string& folder)
{
    _mapFolder = folder;
    if (folder.empty()) return;

    _map = std::make_unique<Map>();
    if (_map->Load(folder))
        std::cout << "[GameScene] Map loaded: " << folder
                  << "  " << _map->GetWidth() << "x" << _map->GetHeight()
                  << " unit=" << _map->GetUnitSize() << "\n";
    else
    {
        std::cerr << "[GameScene] Map load failed: " << folder << "\n";
        _map.reset();
    }
}

void GameScene::Init()
{
    std::cout << "[GameScene] Init cho user: " << _username
              << " spawn=(" << _spawnX << "," << _spawnY << ")"
              << " bodyID=" << _appearance.bodyID
              << " hairID=" << _appearance.hairID << "\n";

    // Đặt giới hạn world = kích thước màn hình
    _ecsScene.GetPhysicsSystem().SetWorldSize((float)_screenW, (float)_screenH);

    // Load map (thử tìm map theo tên "Home" mặc định ở thư mục maps/)
    // Ưu tiên: maps/<folder> relative đến thư mục chạy
    {
        std::string tryPath = "res/paks/maps/Home";
        if (!_mapFolder.empty()) tryPath = _mapFolder;
        if (std::filesystem::exists(tryPath))
            LoadMap(tryPath);
        else
            std::cout << "[GameScene] Không tìm thấy map folder: " << tryPath << "\n";
    }

    // Gán map cho PhysicsSystem để client-side collision hoạt động
    if (_map)
        _ecsScene.GetPhysicsSystem().SetMap(_map.get());

    // Tạo nhân vật chính tại đúng vị trí server trả về
    SpawnPlayer();

    // Kết nối GameServer (nếu có token)
    if (!_sessionToken.empty())
    {
        if (_gameClient.Connect(_sessionToken, _username, _spawnX, _spawnY))
            std::cout << "[GameScene] Kết nối GameServer OK. PlayerID="
                      << _gameClient.GetMyPlayerID() << "\n";
        else
            std::cerr << "[GameScene] Không thể kết nối GameServer (chạy offline).\n";
    }
    else
    {
        std::cout << "[GameScene] Không có session token — chạy offline.\n";
    }
}

void GameScene::Update(float dt)
{
    // ── F1 toggle debug collision overlay ────────────────────
    {
        bool f1Now = (glfwGetKey(_window, GLFW_KEY_F1) == GLFW_PRESS);
        if (f1Now && !_f1Pressed)
        {
            _debugCollision = !_debugCollision;
            std::cout << "[Debug] Collision overlay: "
                      << (_debugCollision ? "ON" : "OFF") << "\n";
        }
        _f1Pressed = f1Now;
    }

    // ── Kiểm tra bị kick bởi server ──────────────────────────
    if (_gameClient.WasKicked())
    {
        std::string reason = _gameClient.GetKickReason();
        std::cout << "[GameScene] Bị kick: " << reason << "\n";
        if (_onKicked)
            _onKicked(reason);
        return; // Không update gì thêm, chờ scene chuyển
    }

    // Chạy toàn bộ ECS pipeline
    _ecsScene.Update(dt);

    // ── Lazy-load map regions quanh vị trí player ─────────
    if (_map)
    {
        float px, py;
        if (GetPlayerPos(px, py))
        {
            float regionSize = _map->GetUnitSize(); // = unitSize * tilesPerRegion

            int rx = (int)(px / regionSize);
            int ry = (int)(py / regionSize);

            // số region cần để cover màn hình
            int loadX = (int)std::ceil(_screenW / regionSize * 0.5f) + 1;
            int loadY = (int)std::ceil(_screenH / regionSize * 0.5f) + 1;

            _map->LoadRegionAround(*_renderer, px, py, std::max(loadX, loadY) + 1);
            _map->UnloadRegionsFar(px, py, std::max(loadX, loadY) + 1);

            // Cập nhật camera: player ở giữa màn hình
            float camX = px - _screenW * 0.5f;
            float camY = py - _screenH * 0.5f;
            _renderer->SetCamera(camX, camY);
        }
    } // end if (_map)

    // Gửi vị trí + animState nhân vật lên server mỗi _sendInterval giây
    if (_gameClient.IsConnected())
    {
        _sendTimer += dt;
        if (_sendTimer >= _sendInterval)
        {
            _sendTimer = 0.f;
            float px, py;
            if (GetPlayerPos(px, py))
            {
                // Lấy animation hiện tại của nhân vật chính → map sang AnimState
                uint8_t animSt = ANIM_IDLE_DOWN;
                const Object* playerObj = _ecsScene.FindObject("Player");
                if (playerObj)
                {
                    SpriteComponent* spr = playerObj->GetComponent<SpriteComponent>();
                    if (spr)
                    {
                        const std::string& curAnim = spr->GetCurrentAnimation();
                        if      (curAnim == "walk_left")   animSt = ANIM_WALK_LEFT;
                        else if (curAnim == "walk_right")  animSt = ANIM_WALK_RIGHT;
                        else if (curAnim == "walk_up")     animSt = ANIM_WALK_UP;
                        else if (curAnim == "walk_down")   animSt = ANIM_WALK_DOWN;
                        else if (curAnim == "idle_left")   animSt = ANIM_IDLE_LEFT;
                        else if (curAnim == "idle_right")  animSt = ANIM_IDLE_RIGHT;
                        else if (curAnim == "idle_up")     animSt = ANIM_IDLE_UP;
                        else                               animSt = ANIM_IDLE_DOWN;
                    }
                }
                // Lấy hitbox từ SpriteComponent frame hiện tại
                int hx = 0, hy = 0, hw = 16, hh = 24; // fallback default
                if (playerObj)
                {
                    SpriteComponent* spr2 = playerObj->GetComponent<SpriteComponent>();
                    if (spr2)
                        spr2->GetHitbox(hx, hy, hw, hh);
                }
                _gameClient.SendMove(px, py, animSt,
                                     (int16_t)hx, (int16_t)hy,
                                     (int16_t)hw, (int16_t)hh);
            }
        }
    }

    // ── Đồng bộ remote sprite cache với danh sách player hiện tại ─────────
    {
        auto remotePlayers = _gameClient.GetRemotePlayers();

        // Thêm sprite cho player mới join
        for (auto& [id, rp] : remotePlayers)
        {
            if (_remoteSprites.find(id) == _remoteSprites.end())
                LoadRemoteSprite(id);
        }

        // Xóa sprite của player đã leave
        for (auto it = _remoteSprites.begin(); it != _remoteSprites.end(); )
        {
            if (remotePlayers.find(it->first) == remotePlayers.end())
                it = _remoteSprites.erase(it);
            else
                ++it;
        }

        // Cập nhật position interpolation + animation state + frame cho mỗi remote sprite
        // Lerp speed: tốc độ đuổi theo target position (pixels/s factor)
        // 10.0 = mượt, cao hơn = ít trễ hơn nhưng giật hơn
        constexpr float LERP_SPEED = 12.0f;
        const float lerpT = 1.f - std::exp(-LERP_SPEED * dt); // exponential lerp

        for (auto& [id, sprPtr] : _remoteSprites)
        {
            auto rpIt = remotePlayers.find(id);
            if (rpIt != remotePlayers.end())
            {
                // Lerp renderX/Y về target (network position)
                float targetX = rpIt->second.x;
                float targetY = rpIt->second.y;

                // Nếu khoảng cách quá lớn (teleport), snap ngay
                float dx = targetX - sprPtr->renderX;
                float dy = targetY - sprPtr->renderY;
                if (dx * dx + dy * dy > 200.f * 200.f)
                {
                    sprPtr->renderX = targetX;
                    sprPtr->renderY = targetY;
                }
                else
                {
                    sprPtr->renderX += (targetX - sprPtr->renderX) * lerpT;
                    sprPtr->renderY += (targetY - sprPtr->renderY) * lerpT;
                }

                // Map AnimState → tên animation string
                std::string animName;
                switch (rpIt->second.animState)
                {
                    case ANIM_IDLE_LEFT:   animName = "idle_left";   break;
                    case ANIM_IDLE_RIGHT:  animName = "idle_right";  break;
                    case ANIM_IDLE_UP:     animName = "idle_up";     break;
                    case ANIM_WALK_LEFT:   animName = "walk_left";   break;
                    case ANIM_WALK_RIGHT:  animName = "walk_right";  break;
                    case ANIM_WALK_UP:     animName = "walk_up";     break;
                    case ANIM_WALK_DOWN:   animName = "walk_down";   break;
                    default:               animName = "idle_down";   break;
                }
                // Chỉ gọi PlayAnimation khi tên animation thay đổi
                if (sprPtr->sprite.GetCurrentAnimation() != animName)
                    sprPtr->sprite.PlayAnimation(animName, false);
            }
            sprPtr->sprite.Update(dt);
        }
    }
}


#include "Scene/SceneManager.h"
#include "LoginScene.h"
void GameScene::Render()
{
    if (!_renderer) return;

    // ════════════════════════════════════════════════════════════
    //  WORLD SPACE (camera transform active)
    // ════════════════════════════════════════════════════════════
    _renderer->BeginWorldDraw();

    // ── 0. Map tiles (dưới mọi entity) ───────────────────────
    if (_map)
    {
        if (_debugCollision)
            _map->DrawGrid(*_renderer, _map->GetUnitSize());
        int layers = _map->GetLayerCount();
        for (int l = 0; l < layers; l++)
            _map->DrawLayer(*_renderer, l);



        // Debug: vẽ obstacle //// đỏ (F1 toggle)
        if (_debugCollision)
            {
                _map->DrawObstaclesDebug(*_renderer);
             }
            // ── 1. ECS entities (nhân vật chính, ...) ────────────────
        _ecsScene.Render();

                if (_debugCollision)
            {
                //Vẽ hit box cho player
                float px = 0, py = 0;
                if (GetPlayerPos(px, py))
                {
                    const Object* playerObj = _ecsScene.FindObject("Player");
                    if (playerObj)
                    {
                        SpriteComponent* spr = playerObj->GetComponent<SpriteComponent>();
                        if (spr)
                        {
                            int hx, hy, hw, hh;
                            spr->GetHitbox(hx, hy, hw, hh);
                            _renderer->DrawHatchRect(px + hx, py + hy, hw, hh,
                                                     1.f, 0.f, 0.f, 0.6f);
                        }
                    }
                }
             }

    }
    // ── 2. Remote players ─────────────────────────────────────
    auto remotePlayers = _gameClient.GetRemotePlayers();
    for (auto& [id, rp] : remotePlayers)
    {
        float renderX = rp.x;
        float renderY = rp.y;
        bool  drewSprite = false;

        auto it = _remoteSprites.find(id);
        if (it != _remoteSprites.end() && it->second)
        {
            renderX = it->second->renderX;
            renderY = it->second->renderY;

            if (it->second->sprite.IsLoaded())
            {
                it->second->sprite.RenderAt(renderX, renderY);
                drewSprite = true;
            }
        }

        if (!drewSprite)
        {
            _renderer->DrawRect(renderX - 16.f, renderY - 24.f,
                                32.f, 48.f,
                                0.5f, 0.5f, 0.9f, 0.85f);
        }

        // Tên player trong world space (dịch chuyển cùng camera)
        _renderer->DrawText(rp.username, renderX - 20.f, renderY - 36.f, 0.35f);
    }

    _renderer->EndWorldDraw();

    // ════════════════════════════════════════════════════════════
    //  SCREEN SPACE (HUD — không bị camera ảnh hưởng)
    // ════════════════════════════════════════════════════════════

    // ── 3. HUD ────────────────────────────────────────────────
    _renderer->DrawText("Nhan vat: " + _username, 10.f, 20.f, 0.5f);

    if (_gameClient.IsConnected())
    {
        std::string onlineInfo = "Online | ID:" +
            std::to_string(_gameClient.GetMyPlayerID()) +
            " | Players: " + std::to_string(remotePlayers.size() + 1);
        _renderer->DrawText(onlineInfo, 10.f, 50.f, 0.4f);
    }
    else
    {
        _renderer->DrawText("Offline mode", 10.f, 50.f, 0.4f);
    }

    // Debug: hiện vị trí player + camera
    {
        float px = 0, py = 0;
        GetPlayerPos(px, py);
        std::string dbg = "Pos(" + std::to_string((int)px) + "," + std::to_string((int)py) + ")"
            + " Cam(" + std::to_string((int)_renderer->GetCameraX()) + ","
            + std::to_string((int)_renderer->GetCameraY()) + ")";
        _renderer->DrawText(dbg, 10.f, 80.f, 0.35f);
    }
}

void GameScene::Destroy()
{
    _remoteSprites.clear();
    _gameClient.Disconnect();
    std::cout << "[GameScene] Destroyed.\n";
}

// ── SpawnPlayer ───────────────────────────────────────────────────────────────

void GameScene::SpawnPlayer()
{
    Object* player = _ecsScene.CreateObject("Player");

    // TransformComponent: spawn tại vị trí từ server
    player->AddComponent<TransformComponent>(_spawnX, _spawnY);

    // SpriteComponent
    SpriteComponent* spr = player->AddComponent<SpriteComponent>(_renderer);
    const std::string sprVFS  = "res/char/main/main.spr";
    const std::string sprDisk = "res/paks/res/char/main/main.spr";
    if (VFS::Get().Exists(sprVFS))
    {
        if (!spr->LoadFromVFS(sprVFS))
            std::cerr << "[GameScene] LoadFromVFS SPR thất bại.\n";
    }
    else
    {
        if (!spr->LoadFromSPR(sprDisk))
            std::cerr << "[GameScene] Không tìm thấy sprite nhân vật: "
                      << sprDisk << "\n";
    }

    // ScriptComponent
    const std::string scriptVFS  = "scripts/entities/player/player.lua";
    const std::string scriptDisk = "res/paks/scripts/entities/player/player.lua";
    ScriptComponent* sc = player->AddComponent<ScriptComponent>(
        scriptDisk, _window);

    if (VFS::Get().Exists(scriptVFS))
    {
        if (!sc->LoadFromVFS(scriptVFS))
            std::cerr << "[GameScene] LoadFromVFS Script thất bại.\n";
    }
    else
    {
        if (!sc->Load())
            std::cerr << "[GameScene] Không nạp được script: "
                      << scriptDisk << "\n";
    }

    std::cout << "[GameScene] SpawnPlayer tại ("
              << _spawnX << ", " << _spawnY << ")"
              << " bodyID=" << _appearance.bodyID
              << " hairID=" << _appearance.hairID << "\n";
}

// ── LoadRemoteSprite ──────────────────────────────────────────────────────────

void GameScene::LoadRemoteSprite(uint32_t playerID)
{
    auto sprEntry = std::make_unique<RemotePlayerSprite>(_renderer);

    const std::string sprVFS  = "res/char/main/main.spr";
    const std::string sprDisk = "res/paks/res/char/main/main.spr";

    bool loaded = false;
    if (VFS::Get().Exists(sprVFS))
        loaded = sprEntry->sprite.LoadFromVFS(sprVFS);
    else
        loaded = sprEntry->sprite.LoadFromSPR(sprDisk);

    if (!loaded)
        std::cerr << "[GameScene] LoadRemoteSprite: không load được sprite cho playerID="
                  << playerID << "\n";
    else
        std::cout << "[GameScene] LoadRemoteSprite OK cho playerID=" << playerID << "\n";

    // Khởi tạo renderX/Y = vị trí thực ngay lúc spawn (tránh lerp từ 0,0)
    auto remotePlayers = _gameClient.GetRemotePlayers();
    auto rpIt = remotePlayers.find(playerID);
    if (rpIt != remotePlayers.end())
    {
        sprEntry->renderX = rpIt->second.x;
        sprEntry->renderY = rpIt->second.y;
    }

    _remoteSprites[playerID] = std::move(sprEntry);
}

// ── GetPlayerPos ──────────────────────────────────────────────────────────────

bool GameScene::GetPlayerPos(float& outX, float& outY) const
{
    const Object* player = _ecsScene.FindObject("Player");
    if (!player) return false;

    TransformComponent* tc = player->GetComponent<TransformComponent>();
    if (!tc) return false;

    outX = tc->x;
    outY = tc->y;
    return true;
}
