#pragma once
#include "../Object/Object.h"
#include "../Components/TransformComponent.h"
#include "../../ResManager/Map/Map.h"
#include <vector>

// PhysicsSystem: va chạm AABB với obstacle từ Map (RegionS)
// Mỗi entity cần có TransformComponent.
// Kích thước AABB của entity được set qua SetEntitySize().
class PhysicsSystem
{
public:
    // entityW/H: kích thước hitbox mặc định của entity (pixels)
    PhysicsSystem(float entityW = 32.f, float entityH = 32.f)
        : _entityW(entityW), _entityH(entityH), _map(nullptr) {}

    // Gán map để lấy obstacle data
    void SetMap(Map* map) { _map = map; }

    // Thay đổi kích thước hitbox mặc định
    void SetEntitySize(float w, float h) { _entityW = w; _entityH = h; }

    // Tương thích ngược: giữ lại SetWorldSize nhưng không dùng để clamp nữa
    void SetWorldSize(float /*w*/, float /*h*/) {}

    void Update(float dt, const std::vector<Object*>& objects)
    {
        for (auto* obj : objects)
        {
            if (!obj || !obj->IsActive()) continue;
            auto* tf = obj->GetComponent<TransformComponent>();
            if (!tf) continue;

            if (!_map) continue; // chưa có map thì bỏ qua

            // Slide collision: check từng trục độc lập
            // MovementSystem đã cập nhật tf->x, tf->y theo velocity.
            // Ta lưu lại vị trí hiện tại (đã move), rồi rollback từng trục.
            float curX = tf->x;          // X sau khi move
            float curY = tf->y;          // Y sau khi move
            float prevX = curX - tf->vx * dt;  // X trước khi move
            float prevY = curY - tf->vy * dt;  // Y trước khi move

            float resolvedX = curX;
            float resolvedY = curY;

            // Helper lambda: thử CheckCollisionClient trước, fallback CheckCollision (server)
            auto collide = [&](float x, float y) -> bool {
                bool hit = _map->CheckCollisionClient(x, y, _entityW, _entityH);
                if (!hit) hit = _map->CheckCollision(x, y, _entityW, _entityH);
                return hit;
            };

            // ── Check trục X (giữ Y cũ) ──────────────────────
            if (collide(curX, prevY))
            {
                resolvedX = prevX;  // rollback X
                tf->vx    = 0.f;
            }

            // ── Check trục Y (dùng X đã resolve) ─────────────
            if (collide(resolvedX, curY))
            {
                resolvedY = prevY;  // rollback Y
                tf->vy    = 0.f;
            }

            // ── Final: nếu vẫn collide thì giữ vị trí cũ hoàn toàn
            if (collide(resolvedX, resolvedY))
            {
                resolvedX = prevX;
                resolvedY = prevY;
                tf->vx    = 0.f;
                tf->vy    = 0.f;
            }

            tf->x = resolvedX;
            tf->y = resolvedY;
        }
    }

private:
    float _entityW; // hitbox width
    float _entityH; // hitbox height
    Map*  _map;     // server map (RegionS obstacles)
};
