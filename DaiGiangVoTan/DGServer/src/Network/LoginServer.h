#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include "../Auth/UserManager.h"
#include "../../CommonProtocol/LoginProtocol.h"

// ================================================================
//  SessionManager — lưu token → username mapping
//  1 session per username: login lại sẽ vô hiệu hóa token cũ
//  Thread-safe (GameServer cần validate token từ thread khác)
// ================================================================
class SessionManager
{
public:
    // Tạo token mới cho username.
    // Nếu username đã có session cũ → xóa token cũ trước.
    std::string CreateSession(const std::string& username);

    // Kiểm tra token hợp lệ, trả về username (rỗng nếu invalid)
    std::string ValidateToken(const std::string& token);

    // Xóa session (logout / disconnect)
    void RemoveSession(const std::string& token);

    // Singleton
    static SessionManager& Get()
    {
        static SessionManager inst;
        return inst;
    }

private:
    std::unordered_map<std::string, std::string> _tokenToUser;  // token → username
    std::unordered_map<std::string, std::string> _userToToken;  // username → token (1 session)
    std::mutex _mutex;
};

// ================================================================
//  LoginServer
// ================================================================
class LoginServer
{
public:
    LoginServer(int port = 9000, const std::string& usersFile = "users.txt");
    ~LoginServer();

    bool Start();
    void AcceptOnce();
    void Stop();
    bool IsRunning() const { return _running; }

private:
    int         _port;
    bool        _running = false;
    uintptr_t   _listenSock;

    UserManager _userManager;

    void HandleClient(uintptr_t clientSock);
};
