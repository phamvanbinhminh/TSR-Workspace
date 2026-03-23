#pragma once
#include "IScene.h"
#include "../SExportEngineAPI.h"

#include <memory>
#include <vector>

// ============================================================
// SceneManager — quản lý stack của IScene (generic)
// ============================================================
// - Push: thêm scene mới lên top, Init() ngay lập tức
// - Pop:  Destroy() top rồi xoá
// - Replace: Pop rồi Push (chuyển scene không quay lại)
// - Update/Render: duyệt stack từ bottom lên theo ShouldUpdateBelow/ShouldRenderBelow
//
// Singleton: SceneManager::Get()
// ============================================================

class SENGINE_API SceneManager
{
public:
    static SceneManager& Get();

    // Push scene mới, Init() ngay
    void Push(std::unique_ptr<IScene> scene);

    // Destroy top scene rồi pop
    void Pop();

    // Destroy top rồi push scene mới (không stack lại)
    void Replace(std::unique_ptr<IScene> scene);

    // Lấy scene đang active (top of stack)
    IScene* Current() const;

    // Kiểm tra stack rỗng
    bool IsEmpty() const;

    // Số scene trên stack
    int Count() const;

    // Update toàn bộ stack (tuân theo ShouldUpdateBelow)
    void Update(float dt);

    // Render toàn bộ stack (tuân theo ShouldRenderBelow)
    void Render();

    // Xoá toàn bộ stack (Destroy từng scene)
    void Clear();

private:
    SceneManager() = default;

    std::vector<std::unique_ptr<IScene>> _stack;

    // Pending operations (để tránh invalidate iterator trong Update)
    enum class PendingOp { None, Push, Pop, Replace, Clear };
    PendingOp                  _pendingOp = PendingOp::None;
    std::unique_ptr<IScene>    _pendingScene;

    void FlushPending();
};
