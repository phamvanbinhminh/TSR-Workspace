#include "LoginClient.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <cstring>

LoginClient::LoginClient(const std::string& serverIP, int port)
    : _serverIP(serverIP), _port(port)
{
    InitWSA();
}

LoginClient::~LoginClient()
{
    CleanupWSA();
}

bool LoginClient::InitWSA()
{
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) return false;
    _wsaInitialized = true;
    return true;
}

void LoginClient::CleanupWSA()
{
    if (_wsaInitialized)
    {
        WSACleanup();
        _wsaInitialized = false;
    }
}

bool LoginClient::SendLogin(const std::string& username, const std::string& password,
                             S2C_LoginResult& outResult)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        std::cerr << "[LoginClient] Không tạo được socket.\n";
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((u_short)_port);
    inet_pton(AF_INET, _serverIP.c_str(), &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        std::cerr << "[LoginClient] Không thể kết nối đến " << _serverIP
                  << ":" << _port << "\n";
        closesocket(sock);
        return false;
    }

    // Build C2S_Login
    C2S_Login req{};
    InitPacket(req, (uint16_t)C2S_LOGIN);
    strncpy_s(req.username, username.c_str(), sizeof(req.username) - 1);
    strncpy_s(req.password, password.c_str(), sizeof(req.password) - 1);

    send(sock, (const char*)&req, sizeof(req), 0);

    // Nhận S2C_LoginResult
    int r = recv(sock, (char*)&outResult, sizeof(S2C_LoginResult), MSG_WAITALL);
    closesocket(sock);

    if (r != sizeof(S2C_LoginResult)) return false;

    // Lưu session token nếu login OK
    if (outResult.result == LOGIN_SUCCESS)
    {
        outResult.sessionToken[63] = '\0';
        _sessionToken = outResult.sessionToken;
        std::cout << "[LoginClient] Login OK. Token=" << _sessionToken << "\n";
    }

    return true;
}
