#pragma once
#include "Scene/IScene.h"
#include "Renderer/Renderer.h"
#include "../Network/LoginClient.h"
#include "../../../CommonProtocol/Appearance.h"

#include <string>

struct GLFWwindow;

enum class LoginState
{
    Idle,
    Sending,
    Success,
    Failed,
};

enum class InputField
{
    Username,
    Password,
};

// ============================================================
// LoginScene — màn hình đăng nhập
// Khi login thành công: chuyển sang GameScene với đầy đủ
//   sessionToken, spawnX/Y, Appearance từ server
// ============================================================
class LoginScene : public IScene
{
public:
    LoginScene(GLFWwindow* window, Renderer* renderer, int screenW, int screenH);

    void Init()           override;
    void Update(float dt) override;
    void Render()         override;
    void Destroy()        override;

    void OnChar(unsigned int codepoint);

    const std::string& GetUsername()     const { return _username; }
    const std::string& GetSessionToken() const { return _sessionToken; }

private:
    GLFWwindow*  _window   = nullptr;
    Renderer*    _renderer = nullptr;
    int          _screenW  = 1280;
    int          _screenH  = 720;

    std::string  _username;
    std::string  _password;
    InputField   _activeField = InputField::Username;

    LoginState   _state = LoginState::Idle;
    std::string  _statusMsg;
    std::string  _sessionToken;

    // Data nhân vật nhận từ server sau khi login thành công
    float        _charPosX    = 640.f;
    float        _charPosY    = 360.f;
    Appearance   _appearance;

    float        _enterCooldown = 0.0f;

    LoginClient  _loginClient;

    void HandleSpecialKeys();
    void DoLogin();
};
