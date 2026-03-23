#include "LoginScene.h"
#include "GameScene.h"
#include "Scene/SceneManager.h"

#include <GLFW/glfw3.h>
#include <iostream>

// Static pointer để dùng trong GLFW char callback
static LoginScene* g_loginScene = nullptr;

static void CharCallback(GLFWwindow* /*window*/, unsigned int codepoint)
{
    if (g_loginScene)
        g_loginScene->OnChar(codepoint);
}

// ── Constructor ───────────────────────────────────────────────────────────────

LoginScene::LoginScene(GLFWwindow* window, Renderer* renderer,
                       int screenW, int screenH)
    : _window(window)
    , _renderer(renderer)
    , _screenW(screenW)
    , _screenH(screenH)
    , _loginClient("127.0.0.1", 9000)
{
}

// ── IScene interface ─────────────────────────────────────────────────────────

void LoginScene::Init()
{
    g_loginScene = this;
    glfwSetCharCallback(_window, CharCallback);
    _statusMsg = u8"Nhập tên tài khoản và mật khẩu, Enter để đăng nhập";
    std::cout << "[LoginScene] Sẵn sàng.\n";
}

void LoginScene::Update(float dt)
{
    if (_enterCooldown > 0.0f)
        _enterCooldown -= dt;

    if (_state == LoginState::Sending)
        return;

    // Sau khi login thành công → chuyển sang GameScene
    // Truyền đầy đủ: sessionToken, spawnX/Y, Appearance từ server
    if (_state == LoginState::Success)
    {
        auto gameScene = std::make_unique<GameScene>(
            _window, _renderer,
            _username,
            _sessionToken,
            "127.0.0.1",   // GameServer IP
            _screenW, _screenH,
            _charPosX,     // vị trí lần cuối từ server
            _charPosY,
            _appearance    // ngoại trang nhân vật từ server
        );

        // Khi bị kick (login ở nơi khác) → quay về LoginScene
        gameScene->SetOnKickedCallback([this](const std::string& reason) {
            // Chuyển về LoginScene từ game thread an toàn qua SceneManager
            SceneManager::Get().Replace(
                std::make_unique<LoginScene>(_window, _renderer, _screenW, _screenH));
        });

        SceneManager::Get().Replace(std::move(gameScene));
        return;
    }

    HandleSpecialKeys();
}

void LoginScene::Render()
{
    if (!_renderer) return;

    float cx     = (float)_screenW * 0.5f;
    float startY = (float)_screenH * 0.3f;
    float lineH  = 40.0f;
    float scale  = 0.6f;

    _renderer->DrawText(u8"=== ĐẠI GIANG VÔ TẬN ===",
                        cx - 220.0f, startY, scale);

    // Username
    std::string userLine = u8"Tài khoản : " + _username;
    if (_activeField == InputField::Username && _state == LoginState::Idle)
        userLine += "_";
    _renderer->DrawText(userLine, cx - 200.0f, startY + lineH * 2, scale);

    // Password (ẩn bằng *)
    std::string passDisplay(_password.size(), '*');
    std::string passLine = u8"Mật khẩu  : " + passDisplay;
    if (_activeField == InputField::Password && _state == LoginState::Idle)
        passLine += "_";
    _renderer->DrawText(passLine, cx - 200.0f, startY + lineH * 3, scale);

    _renderer->DrawText(
        u8"[Tab] Chuyển trường  [Enter] Đăng nhập  [Backspace] Xóa",
        cx - 300.0f, startY + lineH * 5, scale * 0.8f);

    _renderer->DrawText(_statusMsg, cx - 300.0f, startY + lineH * 6.5f, scale * 0.9f);
}

void LoginScene::Destroy()
{
    if (_window)
        glfwSetCharCallback(_window, nullptr);
    g_loginScene = nullptr;
    std::cout << "[LoginScene] Destroyed.\n";
}

// ── Input helpers ─────────────────────────────────────────────────────────────

void LoginScene::HandleSpecialKeys()
{
    if (!_window) return;

    static bool tabPressed  = false;
    static bool backPressed = false;

    // Tab: chuyển field
    if (glfwGetKey(_window, GLFW_KEY_TAB) == GLFW_PRESS)
    {
        if (!tabPressed)
        {
            tabPressed = true;
            _activeField = (_activeField == InputField::Username)
                         ? InputField::Password : InputField::Username;
        }
    }
    else tabPressed = false;

    // Backspace: xóa ký tự cuối
    if (glfwGetKey(_window, GLFW_KEY_BACKSPACE) == GLFW_PRESS)
    {
        if (!backPressed)
        {
            backPressed = true;
            if (_activeField == InputField::Username && !_username.empty())
                _username.pop_back();
            else if (_activeField == InputField::Password && !_password.empty())
                _password.pop_back();
        }
    }
    else backPressed = false;

    // Enter: login
    if (glfwGetKey(_window, GLFW_KEY_ENTER)    == GLFW_PRESS ||
        glfwGetKey(_window, GLFW_KEY_KP_ENTER) == GLFW_PRESS)
    {
        if (_enterCooldown <= 0.0f)
        {
            _enterCooldown = 0.5f;
            DoLogin();
        }
    }
}

void LoginScene::OnChar(unsigned int codepoint)
{
    if (_state == LoginState::Sending || _state == LoginState::Success)
        return;
    if (codepoint < 32 || codepoint > 126) return;

    char c = static_cast<char>(codepoint);
    if (_activeField == InputField::Username && _username.size() < 31)
        _username += c;
    else if (_activeField == InputField::Password && _password.size() < 31)
        _password += c;
}

void LoginScene::DoLogin()
{
    if (_username.empty() || _password.empty())
    {
        _statusMsg = u8"Vui lòng nhập đầy đủ tên tài khoản và mật khẩu!";
        _state = LoginState::Failed;
        return;
    }

    _state = LoginState::Sending;
    _statusMsg = u8"Đang kết nối đến server...";

    S2C_LoginResult result{};
    bool ok = _loginClient.SendLogin(_username, _password, result);

    if (!ok)
    {
        _state = LoginState::Failed;
        _statusMsg = u8"Không thể kết nối đến server!";
        return;
    }

    if (result.result == LOGIN_SUCCESS)
    {
        _state = LoginState::Success;

        // Session token
        result.sessionToken[sizeof(result.sessionToken) - 1] = '\0';
        _sessionToken = result.sessionToken;

        // Vị trí nhân vật lần cuối
        _charPosX = result.posX;
        _charPosY = result.posY;

        // Ngoại trang nhân vật
        _appearance = result.appearance;

        _statusMsg = u8"Đăng nhập thành công! Chào mừng " + _username;
        std::cout << "[LoginScene] Đăng nhập thành công: " << _username
                  << " token=" << _sessionToken
                  << " pos=(" << _charPosX << "," << _charPosY << ")"
                  << " bodyID=" << _appearance.bodyID
                  << " hairID=" << _appearance.hairID << "\n";
    }
    else
    {
        _state = LoginState::Failed;
        result.message[sizeof(result.message) - 1] = '\0';
        _statusMsg = std::string(result.message);
        std::cout << "[LoginScene] Đăng nhập thất bại: " << result.message << "\n";
    }
}
