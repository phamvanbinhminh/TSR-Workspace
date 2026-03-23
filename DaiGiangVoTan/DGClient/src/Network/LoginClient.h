#pragma once
#include <string>
#include "../../CommonProtocol/LoginProtocol.h"

// TCP Login Client dùng Winsock2
// Kết nối đến LoginServer, gửi C2S_Login, nhận S2C_LoginResult
// Sau khi login thành công, lưu sessionToken (cookie) để dùng cho GameServer
class LoginClient
{
public:
    LoginClient(const std::string& serverIP = "127.0.0.1", int port = 9000);
    ~LoginClient();

    // Gửi yêu cầu đăng nhập, điền kết quả vào outResult
    bool SendLogin(const std::string& username, const std::string& password,
                   S2C_LoginResult& outResult);

    // Cookie/session token nhận được sau khi login thành công
    const std::string& GetSessionToken() const { return _sessionToken; }

private:
    std::string _serverIP;
    int         _port;
    std::string _sessionToken;   // lưu cookie sau khi login OK

    bool _wsaInitialized = false;
    bool InitWSA();
    void CleanupWSA();
};
