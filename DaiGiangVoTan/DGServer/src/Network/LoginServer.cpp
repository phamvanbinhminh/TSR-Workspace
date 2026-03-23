#include "LoginServer.h"
#include "../Auth/CharacterManager.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <cstring>
#include <sstream>
#include <random>
#include <iomanip>

// ================================================================
//  SessionManager
// ================================================================

static std::string GenerateToken(const std::string& username)
{
    static std::mt19937_64 rng(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t a = dist(rng);
    uint64_t b = dist(rng);
    std::ostringstream oss;
    oss << username << "_"
        << std::hex << std::setw(16) << std::setfill('0') << a
        << std::setw(16) << std::setfill('0') << b;
    return oss.str();
}

std::string SessionManager::CreateSession(const std::string& username)
{
    std::lock_guard<std::mutex> lk(_mutex);

    // ── 1 session per username: vô hiệu hóa token cũ nếu có ──
    auto it = _userToToken.find(username);
    if (it != _userToToken.end())
    {
        std::cout << "[SessionManager] Kick token cũ của: " << username << "\n";
        _tokenToUser.erase(it->second);
        _userToToken.erase(it);
    }

    std::string token = GenerateToken(username);
    _tokenToUser[token]   = username;
    _userToToken[username] = token;
    return token;
}

std::string SessionManager::ValidateToken(const std::string& token)
{
    std::lock_guard<std::mutex> lk(_mutex);
    auto it = _tokenToUser.find(token);
    if (it == _tokenToUser.end()) return "";
    return it->second;
}

void SessionManager::RemoveSession(const std::string& token)
{
    std::lock_guard<std::mutex> lk(_mutex);
    auto it = _tokenToUser.find(token);
    if (it != _tokenToUser.end())
    {
        _userToToken.erase(it->second);
        _tokenToUser.erase(it);
    }
}

// ================================================================
//  LoginServer
// ================================================================

LoginServer::LoginServer(int port, const std::string& usersFile)
    : _port(port)
    , _userManager(usersFile)
    , _listenSock((uintptr_t)INVALID_SOCKET)
{
}

LoginServer::~LoginServer()
{
    Stop();
}

bool LoginServer::Start()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "[LoginServer] WSAStartup thất bại.\n";
        return false;
    }

    _listenSock = (uintptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_listenSock == (uintptr_t)INVALID_SOCKET)
    {
        std::cerr << "[LoginServer] Không thể tạo socket.\n";
        WSACleanup();
        return false;
    }

    int opt = 1;
    setsockopt((SOCKET)_listenSock, SOL_SOCKET, SO_REUSEADDR,
               (const char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((u_short)_port);

    if (bind((SOCKET)_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        std::cerr << "[LoginServer] Bind thất bại trên port " << _port << ".\n";
        closesocket((SOCKET)_listenSock);
        WSACleanup();
        return false;
    }

    if (listen((SOCKET)_listenSock, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "[LoginServer] Listen thất bại.\n";
        closesocket((SOCKET)_listenSock);
        WSACleanup();
        return false;
    }

    _running = true;
    std::cout << "[LoginServer] Lắng nghe trên port " << _port << "...\n";
    return true;
}

void LoginServer::AcceptOnce()
{
    if (!_running) return;

    sockaddr_in clientAddr{};
    int addrLen = sizeof(clientAddr);
    SOCKET clientSock = accept((SOCKET)_listenSock, (sockaddr*)&clientAddr, &addrLen);
    if (clientSock == INVALID_SOCKET)
    {
        if (_running)
            std::cerr << "[LoginServer] Accept thất bại.\n";
        return;
    }

    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
    std::cout << "[LoginServer] Kết nối mới từ " << clientIP << "\n";

    HandleClient((uintptr_t)clientSock);
    closesocket(clientSock);
}

void LoginServer::Stop()
{
    if (_running)
    {
        _running = false;
        closesocket((SOCKET)_listenSock);
        WSACleanup();
        std::cout << "[LoginServer] Server đã dừng.\n";
    }
}

void LoginServer::HandleClient(uintptr_t clientSock)
{
    C2S_Login req{};
    int received = recv((SOCKET)clientSock, (char*)&req, sizeof(C2S_Login), MSG_WAITALL);

    if (received != sizeof(C2S_Login))
    {
        std::cerr << "[LoginServer] Gói tin không hợp lệ (" << received << " bytes).\n";
        return;
    }

    if (req.header.opcode != C2S_LOGIN)
    {
        std::cerr << "[LoginServer] Opcode không hợp lệ: " << req.header.opcode << "\n";
        return;
    }

    req.username[31] = '\0';
    req.password[63] = '\0';

    std::cout << "[LoginServer] Login từ: " << req.username << "\n";

    LoginResult authResult = _userManager.Authenticate(req.username, req.password);

    S2C_LoginResult resp{};
    InitPacket(resp, (uint16_t)S2C_LOGIN_RESULT);
    resp.result = static_cast<int32_t>(authResult);

    if (authResult == LOGIN_SUCCESS)
    {
        std::string username(req.username);

        // ── Tạo session (vô hiệu hóa token cũ nếu đã login) ──
        std::string token = SessionManager::Get().CreateSession(username);
        strncpy_s(resp.sessionToken, token.c_str(), sizeof(resp.sessionToken) - 1);
        strncpy_s(resp.message, "Dang nhap thanh cong!", sizeof(resp.message) - 1);

        // ── Load/tạo character data từ file ──
        CharacterData charData = CharacterManager::Get().LoadOrCreate(username);
        resp.appearance = charData.appearance;
        resp.posX       = charData.posX;
        resp.posY       = charData.posY;
        resp.level      = charData.level;
        resp.mapID      = charData.mapID;

        std::cout << "[LoginServer] Token: " << token << "\n";
        std::cout << "[LoginServer] Character: body=" << charData.appearance.bodyID
                  << " hair=" << charData.appearance.hairID
                  << " pos=(" << charData.posX << "," << charData.posY << ")\n";
    }
    else if (authResult == LOGIN_FAIL_NOT_FOUND)
    {
        strncpy_s(resp.message, "Tai khoan khong ton tai!", sizeof(resp.message) - 1);
    }
    else if (authResult == LOGIN_FAIL_WRONG_PASS)
    {
        strncpy_s(resp.message, "Mat khau khong dung!", sizeof(resp.message) - 1);
    }

    send((SOCKET)clientSock, (const char*)&resp, sizeof(S2C_LoginResult), 0);
}
