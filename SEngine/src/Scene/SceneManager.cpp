#include "SceneManager.h"
#include <stdexcept>

// ── Singleton ────────────────────────────────────────────────────────────────

SceneManager& SceneManager::Get()
{
    static SceneManager instance;
    return instance;
}

// ── Stack operations ─────────────────────────────────────────────────────────

void SceneManager::Push(std::unique_ptr<IScene> scene)
{
    // Đặt pending thay vì thực hiện ngay để tránh invalidate trong Update loop
    _pendingOp    = PendingOp::Push;
    _pendingScene = std::move(scene);
}

void SceneManager::Pop()
{
    _pendingOp    = PendingOp::Pop;
    _pendingScene = nullptr;
}

void SceneManager::Replace(std::unique_ptr<IScene> scene)
{
    _pendingOp    = PendingOp::Replace;
    _pendingScene = std::move(scene);
}

void SceneManager::Clear()
{
    _pendingOp    = PendingOp::Clear;
    _pendingScene = nullptr;
}

IScene* SceneManager::Current() const
{
    if (_stack.empty()) return nullptr;
    return _stack.back().get();
}

bool SceneManager::IsEmpty() const
{
    return _stack.empty();
}

int SceneManager::Count() const
{
    return (int)_stack.size();
}

// ── FlushPending ─────────────────────────────────────────────────────────────

void SceneManager::FlushPending()
{
    if (_pendingOp == PendingOp::None) return;

    switch (_pendingOp)
    {
    case PendingOp::Push:
        if (_pendingScene)
        {
            _pendingScene->Init();
            _stack.push_back(std::move(_pendingScene));
        }
        break;

    case PendingOp::Pop:
        if (!_stack.empty())
        {
            _stack.back()->Destroy();
            _stack.pop_back();
        }
        break;

    case PendingOp::Replace:
        if (!_stack.empty())
        {
            _stack.back()->Destroy();
            _stack.pop_back();
        }
        if (_pendingScene)
        {
            _pendingScene->Init();
            _stack.push_back(std::move(_pendingScene));
        }
        break;

    case PendingOp::Clear:
        // Destroy từ top xuống bottom
        for (int i = (int)_stack.size() - 1; i >= 0; --i)
            _stack[i]->Destroy();
        _stack.clear();
        break;

    default:
        break;
    }

    _pendingOp    = PendingOp::None;
    _pendingScene = nullptr;
}

// ── Update ───────────────────────────────────────────────────────────────────

void SceneManager::Update(float dt)
{
    // Flush TRƯỚC khi update để scene mới được init đúng lúc
    FlushPending();

    if (_stack.empty()) return;

    // Tìm scene thấp nhất cần update (theo ShouldUpdateBelow)
    int startIdx = (int)_stack.size() - 1;
    while (startIdx > 0 && _stack[startIdx]->ShouldUpdateBelow())
        --startIdx;

    for (int i = startIdx; i < (int)_stack.size(); ++i)
        _stack[i]->Update(dt);
}

// ── Render ───────────────────────────────────────────────────────────────────

void SceneManager::Render()
{
    if (_stack.empty()) return;

    // Tìm scene thấp nhất cần render
    int startIdx = (int)_stack.size() - 1;
    while (startIdx > 0 && _stack[startIdx]->ShouldRenderBelow())
        --startIdx;

    for (int i = startIdx; i < (int)_stack.size(); ++i)
        _stack[i]->Render();
}
