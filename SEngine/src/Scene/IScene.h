#pragma once

// ============================================================
// IScene — interface thuần túy cho mọi scene trong engine
// ============================================================
// Không biết gì về game cụ thể.
// Mỗi project tự implement các class kế thừa IScene.
//
// Lifecycle:
//   Push → Init() → [Update/Render loop] → Destroy()
// ============================================================

class IScene
{
public:
    virtual ~IScene() = default;

    // Gọi 1 lần ngay sau khi được push vào SceneManager
    virtual void Init() = 0;

    // Gọi mỗi frame
    virtual void Update(float dt) = 0;

    // Gọi mỗi frame sau Update
    virtual void Render() = 0;

    // Gọi ngay trước khi scene bị pop/replace
    virtual void Destroy() = 0;

    // Có muốn scene bên dưới tiếp tục Update không?
    // (dùng cho transparent/overlay scenes)
    virtual bool ShouldUpdateBelow() const { return false; }

    // Có muốn scene bên dưới tiếp tục Render không?
    virtual bool ShouldRenderBelow() const { return false; }
};
